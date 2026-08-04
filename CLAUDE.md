# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

This is `smartmet-server` — the HTTP server daemon (`smartmetd`) for the SmartMet ecosystem. It is a thin networking layer built on Boost.ASIO that delegates all request handling to dynamically-loaded engines and plugins. The actual content logic lives in `spine` (the core framework library) and the engine/plugin shared objects; this repo owns only the async I/O, connection management, signal handling, and thread pool orchestration.

## Build and test

```bash
make                  # Build the smartmetd binary
make test             # Run the startup test (starts server with minimal.conf, verifies launch, shuts down)
make format           # clang-format all source
```

`make test` runs two integration scripts, not a unit test suite. `test/startup-test.sh` launches `smartmetd` with an empty engine/plugin config (`test/minimal.conf`), waits for the "Launched Synapse server" log line, checks the OPTIONS reply and the keep-alive negotiation through curl, then shuts the server down via SIGTERM and verifies clean exit. `test/conformance-test.sh` starts a second server and drives `test/conformance-test.py` against it over a raw socket for the HTTP/1.1 obligations curl cannot express.

To run the built binary directly for manual testing:

```bash
./smartmetd -d -v -c test/minimal.conf
```

Flags: `-d` = debug mode, `-v` = verbose, `-c` = config file path. Options are parsed by `SmartMet::Spine::Options` (from `spine` library).

## Code architecture

### Source layout

```
main/smartmetd.cpp     — Entry point: signal handling, Reactor + AsyncServer creation, main loop
source/                — Server implementation
include/               — Headers
```

All source is in `SmartMet::Server` namespace.

### Class hierarchy

- **`Connection`** — Abstract base for a single client connection. Owns the SSL socket, read buffer, request/response objects, timeout timer, and references to the three thread pools. Not instantiated directly.
- **`AsyncConnection`** — Concrete connection using half-sync/half-async pattern. Created via `AsyncConnection::create()` factory (private constructor + `enable_shared_from_this`). Handles the full async lifecycle: SSL handshake, chunked/unchunked/gateway reads and writes, gzip compression, client disconnect detection.
- **`Server`** — Abstract server base. Binds the TCP acceptor, initializes SSL context, creates three `ThreadPoolType` executors (admin/slow/fast), stores server-wide options (gzip, timeout, max request size).
- **`AsyncServer`** — Concrete server. Runs `numThreads` ASIO worker threads for network I/O. On accept, creates an `AsyncConnection` and calls `start()`. Shutdown sequence: close acceptor, stop io_service, gracefully shut down thread pools, then shut down Reactor.
- **`Utility`** — Free functions: gzip compression, HTTP date formatting, X-Forwarded-For parsing, request dumping.
- **`Names`** — Extract engine/plugin names from shared object filenames.

### Threading model

The server uses three separate thread pools for request processing, configured in the server config:

- **adminpool** — For `/admin` and `/info` requests (small, fixed)
- **slowpool** — For heavy requests (percentage of hardware threads)
- **fastpool** — For normal requests (percentage of hardware threads)

Network I/O (socket reads/writes, accept loop) runs on a separate set of `server_threads` ASIO worker threads (default 6). The `Reactor` (from `spine`) handles plugin/engine lifecycle in its own thread.

> **High-load rejections are not access-logged.** When the server is overloaded it sends a high-load (`503`) stock reply directly from `AsyncConnection` — either on `isLoadHigh()` or when a pool's task queue is full — and `return`s *before* the request is scheduled to a handler. Access logging lives in `HandlerView::handle()` (per-handler `AccessLogger`), so a rejected request never reaches it and produces **no access-log entry** — only a stdout line (`"Too many active requests, reporting high load"` / `"Backend request queue was full..."`). Don't compute error rates or count `503`s from access logs; high-load events appear only in the system/stdout log. (And the frontend silently retries these on another backend, so they may be invisible client-side too.)

### Persistent connections (HTTP keep-alive)

`AsyncConnection` serves several requests over one socket. The negotiation lives in
`evaluateKeepAlive()` (decision) and `setServerHeaders()` (headers), and the reuse itself in
`finishResponse()`, which every successful terminal write path calls.

The two HTTP versions differ, and the response is therefore sent in the *client's* version
(`Utility::negotiateVersion`) — replying HTTP/1.0 to an HTTP/1.1 request would make the client
assume a non-persistent connection no matter what the headers say:

- **HTTP/1.1** — persistent by default. The server only announces the end of persistence
  with `Connection: close`; it deliberately sends no header when the connection is kept.
- **HTTP/1.0** — has no persistent connections; keep-alive is an extension. The client must
  ask with `Connection: keep-alive` (or the HTTP/1.0-only `Proxy-Connection` variant) and the
  server must confirm with the same header plus `Keep-Alive: timeout=…`, otherwise the client
  reads the body until EOF and hangs on a reused socket.

The connection is **always closed** when the response cannot be framed unambiguously, or when
the request stream cannot be trusted:

| Case | Why |
| --- | --- |
| Gateway responses (`isGatewayResponse`) | The frontend forwards the backend's bytes verbatim; this layer neither knows the framing nor can inject headers. Consistent with what the client sees, since the frontend asks its backends for `Connection: close`. |
| Chunked response to an HTTP/1.0 client | Chunked transfer encoding does not exist in HTTP/1.0. |
| Streamed response whose byte count ≠ the announced `Content-Length` | The client cannot find the start of the next response. |
| 413 / 400 / 408, and shutdown replies | The request was not (or not fully) read, so the unread remainder would be parsed as the next request. |
| Any send error, or a queue-full abort mid-body | The body was cut short. |
| A plugin that sets `Connection: close` itself | Plugin intent wins. |

Stock replies produced *after* a complete parse (404, 503 high load, queue-full at scheduling
time) do keep the connection alive — they carry a `Content-Length` and the request stream is
fully consumed.

> **Pipelining is not supported.** Spine's request grammar ends with `body = *char_`, i.e.
> `parseRequest` consumes the whole buffer as the body. Bytes of a pipelined follow-up request
> get swallowed into the current request instead of being left for the next round. A request
> without `Content-Length` whose parsed body is non-empty is therefore treated as pipelining
> and answered with a close, so the client sees EOF and retries what it got no answer to.
> Removing this guard requires a parser change in `smartmet-library-spine` first.

Timers: the existing `timeout` option still bounds reading the first (and each subsequent)
request. Once a response is finished, `finishResponse()` re-arms the same timer as an *idle*
timeout (`keepalive.timeout`); when that expires the socket is closed **silently** rather than
with a 408, because the client may be writing a request at that very moment. `keepalive.maxrequests`
caps how many requests one connection may serve.

Config (`keepalive.enabled` / `.timeout` / `.maxrequests`, plus the server-wide `maxconnections`)
is read in `Server`'s constructor straight from `Options::itsConfig`, like `logmemoryuse`; the
connections pick it up through the `Server::isKeepAliveEnabled()` accessors.

`maxconnections` bounds how many client connections may be open at once — necessary once
connections are persistent, since they are held open between requests and a slowloris-style client
otherwise accumulates sockets until the process runs out of descriptors. It therefore defaults to
three quarters of the soft `RLIMIT_NOFILE` rather than to a fixed number. Connections over the cap
get a framed 503 and are closed; the limit is logged once per episode, not once per refusal.

### Protocol obligations that come with keep-alive (BS-3475)

A single framing mistake corrupts every later message on the connection, so these are not
cosmetic. Implemented in this repo:

- **One framing, never two.** `startChunkedReply()` drops any `Content-Length` a plugin left
  behind when it sets `Transfer-Encoding: chunked`.
- **Bodyless statuses.** 1xx, 204 and 304 are framed by the status code itself: `startRegularReply()`
  clears the content and removes `Content-Length`/`Transfer-Encoding` for them (`statusHasNoBody()`).
  A client stops reading at the end of the header section, so a body there would be read as the
  next response.
- **`Host` required on HTTP/1.1** → 400 and close (RFC 9112 3.2). Checked in the server rather than
  the parser because HTTP/1.0 requests legitimately have none.
- **`Expect: 100-continue`** → interim `HTTP/1.1 100 Continue`. Decided from the *raw* buffer via
  `Utility::peekRequestHead()` as soon as the header section is complete, because the client is
  withholding the body until it is answered — waiting for a parseable request would deadlock.
  Never sent to an HTTP/1.0 client, which would read the 100 as the final response.
- **Hop-by-hop headers** are removed from the request before handlers see them
  (`stripHopByHopHeaders()`), including whatever `Connection` itself names. Runs *after* the
  keep-alive negotiation, which reads `Connection`.
- **Header section bounded** by `maxheadersize` (16 kB default) → 431, so a client cannot hold a
  connection — and a slot against `maxconnections` — open by dribbling header bytes.

### Pipelining, chunked request bodies, HEAD

These rest on `Spine::HTTP::parseOneRequest()`, which reads **one** message and reports how many
bytes it consumed. `parseRequest()` is still there and unchanged, but it ends its grammar with
`body = *char_`, so it cannot be used on a connection carrying more than one request.

- `handleRead()` erases exactly the consumed prefix from `itsBuffer`. Whatever remains is the
  next request, so `finishResponse()` does **not** clear the buffer: if it is non-empty it
  `boost::asio::post()`s another `handleRead()` instead of going to the socket. Posted rather than
  called directly so a client pipelining many requests does not nest one request/response cycle
  inside the previous one's stack frame. Responses stay in request order because a connection only
  ever has one request in flight.
- Chunked request bodies are decoded by the parser and delivered as an ordinary body with
  `Transfer-Encoding` replaced by the decoded `Content-Length`, so no handler sees chunk framing.
  This is why `Transfer-Encoding` is *not* in the hop-by-hop strip list — it is already gone.
- Request smuggling is rejected in the parser, not resolved by precedence: `Content-Length` with
  `Transfer-Encoding`, conflicting repeats of either, a non-decimal `Content-Length`, and any
  transfer coding other than chunked all give 400 + close.
- **HEAD** is answered as GET with the content omitted (RFC 9110 9.3.2). The request is handed to
  the handler *as a GET* — the plugins do not know the method and several answer only GET and POST
  — with `itsHeadRequest` the only record of it. A streamed or chunked response is collapsed in
  `handleCompletedRead()` rather than started, so the plugin's streamer is never pulled. The cost
  of the rewrite: a HEAD appears as a GET in the per-handler access log.

Still open, in `smartmet-plugin-frontend`: the backend connection pool, framing of forwarded
requests, consuming each backend response body before returning a connection to the pool, and
hop-by-hop stripping on the proxy side.

> **This server now requires a `smartmet-library-spine` that has `parseOneRequest()`.** The
> `BuildRequires`/`Requires` lines in `smartmet-server.spec` still name the older version and must
> be bumped when that spine is released.

### Tests

`make test` runs two scripts. `startup-test.sh` is the launch/shutdown smoke test plus the
keep-alive negotiation seen through curl. `conformance-test.sh` starts a server on port 8079 and
runs `conformance-test.py` against it over a raw socket — the interesting cases (missing `Host`, a
withheld body, a bodyless status, a pipelined request) are exactly the ones a well-behaved client
will not produce, so curl cannot drive them.

### Startup sequence (smartmetd.cpp)

1. Parse command-line options via `Spine::Options`
2. Set `new_handler` (configurable: `bad_alloc` or `terminate`)
3. Create `Spine::Reactor` (the plugin/engine container)
4. Create `AsyncServer` (binds port, starts accept loop)
5. Launch two async tasks: reactor init (loads engines/plugins) and server run (starts thread pools)
6. Main thread enters `select()` loop, handling signals (SIGTERM/SIGINT = shutdown, SIGBUS/SIGWINCH = ignore)

### Configuration

Uses libconfig format (`.conf` files). The server config specifies:
- `port`, `server_threads`, `defaultlogging`, `lazylinking`
- `keepalive` block (`enabled`, `timeout`, `maxrequests`)
- `encryption` block (SSL/TLS cert, key, password)
- `adminpool`, `slowpool`, `fastpool` (thread counts, queue sizes)
- `engines` and `plugins` blocks listing modules to load with their config file paths

See `cnf/smartmet.conf.sample` for a full example.

## Key dependencies

- **`smartmet-library-spine`** — Core framework: `Reactor`, `Options`, `HTTP::Request`/`Response`, `HandlerView`, thread utilities. This is the primary dependency.
- **`smartmet-library-macgyver`** — `Exception`, `AsyncTaskGroup`, `ThreadPool`, ANSI codes, `StaticCleanup`.
- **Boost** — `asio` (networking), `thread`, `program_options`, `regex`, `iostreams`.
- **OpenSSL** — TLS 1.3 support.
- **libdw** (elfutils) — Stack traces via `backward.h` (bundled in `include/`).
- **libconfig++** — Configuration file parsing.

## Linking note

Engine symbols (`SmartMet::Engine::*`) are intentionally unresolved at link time — they are provided at runtime when the server loads engine `.so` files via `dlopen`. The `-rdynamic` linker flag ensures the server exports its own symbols so plugins can call back into it.
