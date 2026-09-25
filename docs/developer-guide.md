# smartmetd developer guide

This guide is for developers who change `smartmet-server`, the `smartmetd` daemon. It
describes how the process starts and stops, how a connection is served from accept to the
last byte, how responses are framed and compressed, the limits and timeouts, and the
pitfalls.

The server is deliberately thin. Loading engines and plugins, routing and admin requests
belong to spine; see the spine
[developer guide](https://github.com/fmidev/smartmet-library-spine/blob/master/docs/developer-guide.md).
Related documents in this repository:

* [CLAUDE.md](../CLAUDE.md): detailed notes on persistent connections, the protocol
  obligations that come with them, pipelining, and why Nagle is off, with measurements.
* [HTTP-KeepAlive-Design.md](HTTP-KeepAlive-Design.md): the original keep-alive design.
* [Admin-Requests.md](Admin-Requests.md): the admin requests from the user's side.
* [SmartMet-Server-Test-Environment.md](SmartMet-Server-Test-Environment.md).

## Contents

1. [Source files](#1-source-files)
2. [Building and testing](#2-building-and-testing)
3. [Process lifecycle](#3-process-lifecycle)
4. [Threads](#4-threads)
5. [A connection's life](#5-a-connections-life)
6. [Reply paths](#6-reply-paths)
7. [Compression](#7-compression)
8. [Persistent connections](#8-persistent-connections)
9. [Limits, timeouts and overload](#9-limits-timeouts-and-overload)
10. [Configuration](#10-configuration)
11. [Known pitfalls](#11-known-pitfalls)

---

## 1. Source files

| File | Contents |
|------|----------|
| `main/smartmetd.cpp` | `main()`: options, signal handling, creating the `Reactor` and the `AsyncServer`, the main loop, shutdown. |
| `source/Server.{cpp}`, `include/Server.h` | Server base: the TCP acceptor, TLS context, the three request thread pools, server-wide settings (compression, timeouts, size limits, keep-alive, `maxconnections`). |
| `source/AsyncServer.cpp`, `include/AsyncServer.h` | The concrete server: `server_threads` Boost.ASIO worker threads, the accept loop, `shutdownServer()`. |
| `source/Connection.cpp`, `include/Connection.h` | Connection base: socket (optionally TLS), read buffer, request and response, timer, pool references. |
| `source/AsyncConnection.cpp`, `include/AsyncConnection.h` | Everything that happens on one connection (§5–§9). |
| `source/Utility.cpp`, `include/Utility.h` | Content-encoding choice and compression, HTTP dates, `X-Forwarded-For` handling, protocol-version negotiation, request dumping. |
| `include/backward.h` | Bundled stack-trace library for `stacktrace = true`. |

## 2. Building and testing

```bash
make                 # smartmetd
make test            # test/startup-test.sh + test/conformance-test.sh
./smartmetd -d -v -c test/minimal.conf
```

* `startup-test.sh` starts the server with an empty engine and plugin list
  (`test/minimal.conf`), waits for `Launched Synapse server`, checks an OPTIONS reply and
  keep-alive negotiation with curl, and checks that SIGTERM gives a clean exit.
* `conformance-test.sh` starts a second server on port 8079 and runs
  `conformance-test.py` over a raw socket, for the HTTP/1.1 cases a well-behaved client
  never produces: missing `Host`, a withheld body with `Expect: 100-continue`, bodyless
  statuses, pipelining.
* End-to-end behaviour through a frontend (connection reuse, chunked responses, the
  `TCP_NODELAY` regression) is tested by `smartmet-plugin-frontend`'s `RunClusterTests`,
  which can run a locally built server with `SMARTMETD=…`.
* `-rdynamic` exports the server's symbols; engine symbols stay unresolved at link time
  and are provided by the engines at load time.

## 3. Process lifecycle

**Startup** (`main()`):

1. `Spine::Options::parse()` reads the command line and the configuration file.
2. `new_handler` selects what happens on allocation failure (`bad_alloc` or
   `terminate`).
3. Signal handlers are installed: SIGTERM, SIGINT, SIGHUP, SIGBUS and SIGWINCH only record
   the signal for the main loop; SIGSEGV prints the active requests and re-raises. With
   `stacktrace = true`, `backward` handles the core signals and prints a stack trace.
4. The `Reactor` is created. Its shutdown-timeout callback `SIGKILL`s the process.
5. The `AsyncServer` is created: it binds the port and prepares TLS and the pools.
6. Two tasks start: **`ini-reactor`** runs `Reactor::init()` (loading and initialising the
   engines and plugins, see the spine guide), and **`srv-run`** waits 3 s, prints
   `Launched Synapse server`, and runs the server. **The server accepts connections before
   the plugins have finished initialising**; requests to a plugin that is still
   initialising get 503 from spine's `callRequestHandler()`, and `/admin?what=waitforready`
   tells when everything is up.

**Main loop.** Every second, the main thread collects finished tasks and checks the last
signal:

* **SIGTERM**: orderly shutdown (below), exit 0.
* **SIGINT**: the same, plus a watchdog: if the shutdown has not finished within
  60 s, or more than five further SIGINT/SIGTERM arrive in quick succession, it
  `abort()`s.
* **SIGBUS, SIGWINCH**: logged and ignored.
* **SIGHUP** (or any other recorded signal): the loop is left without the orderly
  shutdown, and `main()` returns 666.
* If the Reactor reports that its shutdown has finished (for example after a fatal
  initialisation error), the server is shut down and the process exits 0.

**Shutdown** (`AsyncServer::shutdownServer()`): close the acceptor, stop the ASIO
workers, set the three pools to graceful shutdown, shut them down and **destroy them
before the Reactor unloads the plugins**. Queued tasks hold connections whose responses
hold plugin-created streamers, and destroying those after `dlclose()` crashed at exit.
Finally `Reactor::shutdown()` shuts down the plugins and engines.

## 4. Threads

| Threads | Name | Work |
|---------|------|------|
| `server_threads` (default 6) | `srv-wrk-NNNN` | All socket I/O: accept, TLS handshake, reads, writes, timers. |
| `adminpool` | `srv-admin` | Requests whose plugin says `isAdminQuery()`. |
| `slowpool` | `srv-slow` | Requests that are not fast. |
| `fastpool` | `srv-fast` | Requests whose plugin says `queryIsFast()`, and everything the frontend proxies. |
| Reactor tasks | `ini-reactor`, … | Engine and plugin initialisation. |

`maxthreads` accepts an absolute number or a percentage of the hardware threads.
`maxrequeuesize` bounds each pool's queue; a request that finds the queue full gets
503. A pool with zero threads is skipped (slow requests then go to the fast pool, admin
requests to the normal pools). The pool choice is made in `AsyncConnection` after the
request has been parsed; see the spine guide (§7) for how the plugin flags are used.

The request handler runs in a pool thread. Streamed responses are pulled from the
plugin's `ContentStreamer` in pool threads too (`scheduleChunkGetter()`), and written by
the ASIO workers.

## 5. A connection's life

`AsyncServer` accepts a socket, refuses it with a framed 503 if `maxconnections`
connections are already open, and otherwise creates an `AsyncConnection`
(`AsyncConnection::create()`, a `shared_ptr` that keeps itself alive through its
pending handlers). Then:

1. **`start()`**: register the connection in the count, set `TCP_NODELAY`, arm the read
   timeout, and start the TLS handshake if encryption is on.
2. **`handleRead()`**: accumulate bytes, arm the read timeout (`timeout`), enforce
   `maxheadersize` (431) and `maxrequestsize` (413). Answer `Expect: 100-continue` from
   the raw header section as soon as it is complete (`handleExpectContinue()`), because the
   client is withholding the body. Then parse exactly one request with
   `Spine::HTTP::parseOneRequest()`, which also rejects request smuggling (conflicting
   `Content-Length` / `Transfer-Encoding`) with 400 and decodes a chunked request body.
3. **Validate**: HTTP/1.1 without `Host` → 400 and close. Decide keep-alive
   (`evaluateKeepAlive()`), then strip hop-by-hop headers (`stripHopByHopHeaders()`). A
   HEAD request is handed on **as a GET** and only remembered in `itsHeadRequest`.
4. **Dispatch**: find the `HandlerView` (404 if none). If the load is high and the request
   is not an admin query → high-load reply. Otherwise schedule
   `handleCompletedRead()` on the chosen pool (queue full → 503).
5. **`handleCompletedRead()`** (pool thread): cancel the read timer, skip the plugin if
   the client has already disconnected, call the connection-started hooks, run the
   handler (`HandlerView::handle()`, which also writes the access log), then choose a
   reply path (§6).
6. **`finishResponse()`**, after a successful terminal write: close, or reset for the next
   request. If bytes of a pipelined request are already in the buffer, `handleRead()` is
   posted again; otherwise the idle timer (`keepalive.timeout`) is armed and the socket is
   read.

While the request waits in a pool queue, the connection watches for the client going away
(`notifyClientDisconnect()`). If it has gone by the time a pool thread picks the request
up, the plugin is not called at all. The disconnect does not stop a streamer that is
already running.

## 6. Reply paths

After the handler returns, `handleCompletedRead()` picks one of four paths:

| Response | Path | Framing |
|----------|------|---------|
| Buffered content | `startRegularReply()` → `writeRegularReply()` | `Content-Length`; compression possible (§7). One write: head and body together. |
| Streamer, length known | `startStreamReply()` → `getNextChunk()` / `writeStreamReply()` | The plugin's `Content-Length`; a streamer that delivers a different number of bytes closes the connection. |
| Streamer, `setChunked()` | `startChunkedReply()` → `getNextChunkedChunk()` / `writeChunkedReply()` | `Transfer-Encoding: chunked` (any `Content-Length` is dropped). A streamer that fails mid-body gets no terminating `0` chunk, and the socket is closed, so the truncation is visible. |
| Gateway response | `startGatewayReply()` | Bytes passed through verbatim; always closes. (The frontend no longer produces these; see the frontend guide.) |
| HEAD | `startRegularReply()` with the body omitted | As the GET would have been, without running the streamer. |

`setServerHeaders()` adds `Date`, `Server`, `Vary: Accept-Encoding` and the
`Connection` / `Keep-Alive` headers, and answers in the **client's** HTTP version. For
1xx, 204 and 304, the body and length headers are removed (`statusHasNoBody()`).

The head of a streamed response is held back (`itsPendingHeaders`) and sent together with
the first chunk, unless that chunk is slow to come. With `TCP_NODELAY` on, every write
is a packet, so this saves one packet per streamed response (see CLAUDE.md for the
measurements).

## 7. Compression

Only **buffered** responses are compressed, and only when `compress = true`.
`select_content_encoding()` returns nothing when the response already has a
`Content-Encoding`, or its `Content-Type` contains `image/png`, `image/webp` or
`application/pdf`. Otherwise:

1. a request parameter `gzip=1` forces gzip;
2. without `Accept-Encoding`, or when the body is smaller than `compresslimit`, nothing;
3. if `Accept-Encoding` contains `zstd` → zstd (level 3);
4. if it contains `gzip` → gzip.

`Content-Length` is set after compression, and `Vary: Accept-Encoding` is always sent.
Streamed responses (downloads, proxied bodies) are sent as the plugin produced them.

## 8. Persistent connections

HTTP/1.1 connections are persistent unless the client or a plugin says `close`; HTTP/1.0
clients must ask with `Connection: keep-alive` and get it confirmed. The connection is
always closed when the framing cannot be trusted: gateway responses, a chunked response
to HTTP/1.0, a streamer whose length does not match, 400/408/413 replies, send errors,
and a chunked stream that fails mid-body. Requests may be pipelined; responses stay in
order because a connection only has one request in flight.

`keepalive.enabled`, `keepalive.timeout` (idle time between requests, closed silently)
and `keepalive.maxrequests` control it. The full rules, and the reasons for each, are in
CLAUDE.md ("Persistent connections", "Protocol obligations", "Pipelining, chunked request
bodies, HEAD").

When this server is a backend behind a frontend, its `keepalive.timeout` must be longer
than the frontend's `backend.keepalive.idle_timeout`, and `maxconnections` must allow for
the connections each frontend keeps idle (see the frontend guide).

## 9. Limits, timeouts and overload

| Setting | Default | Effect |
|---------|---------|--------|
| `timeout` | 60 s | Reading a request. Expiry → 408 and close. |
| `keepalive.timeout` | 30 s | Idle time between requests. Expiry → silent close. |
| `maxrequestsize` | 131072 | Request size (0 = unlimited). Over → 413 and close. |
| `maxheadersize` | 16 kB | Header section. Over → 431. |
| `maxconnections` | ¾ of the soft `RLIMIT_NOFILE` | Open client connections. Over → framed 503 and close; logged once per episode. |
| `activerequests.*` | 50 / 50 / 100 / 10 | The throttle behind `Reactor::isLoadHigh()`: start with `start_limit` active requests, raise the limit by one every `increase_rate` successful requests up to `limit`, fall back to `restart_limit`. |
| pool `maxrequeuesize` | 100 | Queue length per pool. Full → 503. |

A high-load reply and a full-queue 503 are sent directly from `AsyncConnection`,
**before** the handler runs, so they do not appear in the access log; they are only
printed to stdout. The frontend treats the high-load reply as "try another backend"
and retires the backend for a moment (see the frontend guide).

## 10. Configuration

The main configuration file (libconfig, see `cnf/smartmet.conf.sample`) is read by
`Spine::Options` and, for the connection settings, by `Server`'s constructor. The
server-level keys are `port`, `server_threads`, `encryption.*` (`enabled`,
`certificatefile`, `privatekeyfile`, `passwordfile`, `password`), the three pools,
`activerequests.*`, `keepalive.*`, `maxconnections`, `maxheadersize`, `maxrequestsize`,
`timeout`, `compress`, `compresslimit`, `stalewhilerevalidate`, `staleiferror`,
`dns.*` (client host name resolution), `accesslogdir`, `defaultlogging`,
`logrequests`, `lazylinking`, `stacktrace`, `new_handler`, `logmemoryuse`,
`logmemoryfields`, `debug`, `verbose`, and the `engines` and `plugins` groups
(spine guide §4).

## 11. Known pitfalls

* **SIGHUP does not shut down cleanly.** It leaves the main loop without stopping the
  server or the Reactor, and `main()` returns 666. Use SIGTERM (systemd's default) to stop
  the server.
* **SIGBUS is "ignored".** The handler only records it. A SIGBUS raised by a memory fault
  (for example a memory-mapped file truncated or deleted on NFS) cannot be survived that
  way: returning from the handler repeats the faulting access. Treat SIGBUS in the logs
  as a data-file problem, not as something the server handles.
* **The server accepts requests before the plugins are ready.** Load balancers must use a
  readiness check (the backend plugin's `/` text, or `what=waitforready`), not the open
  port.
* **Content-coding negotiation is a substring match.** `Accept-Encoding` is searched for
  `zstd` and `gzip`; q-values are ignored, so `gzip;q=0` still gets gzip. `gzip=1` in the
  URL forces gzip even for a client that did not ask for it.
* **Overload is not in the access logs.** Count high-load and queue-full replies from the
  stdout log, not the access logs.
* **HEAD is logged as GET** in the per-handler access log, because the handler sees a GET.
* **Streamed responses are never compressed.**
