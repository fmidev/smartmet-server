# HTTP keep-alive support — design

Status: **proposal** (not yet implemented)
Scope: `smartmet-server` only (`AsyncConnection`, `Connection`, `Options`, config)

## Motivation

The server is architecturally **one-request-per-connection**. Every reply is
followed by a socket shutdown, so a client that wants to issue another request
must open a new TCP connection (and, when TLS is terminated here, perform a new
handshake).

This matters because SmartMet is normally fronted by a load balancer / API
gateway — today F5, soon APISIX at FMI, and nginx/Envoy or similar at foreign
installations. All of these keep a **pool of persistent upstream connections**
to their backends (nginx/APISIX `keepalive`, F5 OneConnect) to avoid paying TCP
and TLS setup on every request. The SmartMet server cannot participate: it
forces `Connection: close`, so the gateway is obliged to open and tear down a
fresh connection per request.

Adding HTTP/1.1 keep-alive lets those upstream pools do their job:

- **TLS leg:** the established `ssl::stream` is reused, eliminating a full TLS
  handshake per request — the dominant per-request cost and the main thing the
  gateway pools exist to avoid.
- **Plaintext leg:** removes per-request TCP setup and `accept()` churn on the
  server.

This is a deliberately smaller and lower-risk step than in-process HTTP/2
(see the discussion in the design history): it is pure HTTP/1.1, no framing,
HPACK, or stream multiplexing, and it is confined to one class.

> **Keep-alive is not pipelining.** Upstream connection pools (nginx, APISIX,
> F5) send one request, wait for the full response, then reuse the connection
> for the next request. They do **not** pipeline (send request N+1 before
> response N). This design supports keep-alive only; pipelining is explicitly
> out of scope (see [Non-goals](#non-goals)).

## How connections close today

Closure is **RAII driven by the `shared_ptr` refcount**, not by an explicit
close in the happy path:

1. Every async operation captures `[me = shared_from_this()]`, keeping the
   `AsyncConnection` alive across the callback.
2. `start()` arms the request-timeout timer and the first
   `async_read_some` → `handleRead` (`AsyncConnection.cpp:112`).
3. `handleRead` appends bytes to `itsBuffer` and calls
   `HTTP::parseRequest(itsBuffer)`. On `COMPLETE` it schedules
   `handleCompletedRead` on a thread pool; on a partial parse it re-arms
   `async_read_some` (`AsyncConnection.cpp:168`).
4. `handleCompletedRead` runs the plugin and dispatches to a reply path —
   gateway / chunked / known-length stream / regular
   (`AsyncConnection.cpp:439-461`).
5. The terminal write handler (e.g. `writeRegularReply`, `AsyncConnection.cpp:977`),
   once everything is written, simply **returns without arming another async
   operation**. The last `shared_ptr` drops and `~AsyncConnection`
   (`AsyncConnection.cpp:83-91`) runs `shutdown(shutdown_both)` + `close()`.
6. Error / stock replies close explicitly and synchronously instead
   (`sendSimpleReply`, `AsyncConnection.cpp:1179-1182`).

Because there is only ever one exchange per object, `itsRequest` and
`itsResponse` are allocated **once** in the `Connection` base constructor
(`Connection.cpp:39-40`) and never reset.

The single conceptual change for keep-alive: **at the end of a clean write,
instead of letting the object die, reset per-request state and loop back to
reading the next request on the same socket.**

## Design

### 1. Connection-token decision

`setServerHeaders()` (`AsyncConnection.cpp:1084-1088`) currently sets
`Connection: close` unconditionally on HTTP/1.1. Replace with a decision
function that keeps the connection alive **unless** any of:

- the client requested `Connection: close` (or is HTTP/1.0 without
  `Connection: keep-alive`);
- the server is shutting down (`itsServer->isShutdownRequested()`);
- the per-connection request cap (`keepalive_max_requests`) has been reached;
- the response is not cleanly delimitable on the wire — i.e. neither a known
  `Content-Length` nor `Transfer-Encoding: chunked` (a body framed only by EOF
  *must* close, otherwise the client cannot find the message boundary);
- the response is a **gateway** response (see [Non-goals](#non-goals)).

Emit `Connection: keep-alive` or `Connection: close` to match the decision, and
optionally a `Keep-Alive: timeout=<n>` hint.

### 2. Loop instead of drop

In the terminal write handlers — `writeRegularReply` (`AsyncConnection.cpp:943`),
the known-length stream finalizer `writeStreamReply` (`AsyncConnection.cpp:881`),
and `finalizeChunkedReply` (`AsyncConnection.cpp:671`) — when keep-alive was
negotiated **and** `itsFinalStatus` is clean, call `resetForNextRequest()` and
re-arm the read loop (the same `async_read_some` → `handleRead` used by
`start()`), keeping `me = shared_from_this()` alive. Otherwise fall through to
the existing close behaviour.

### 3. `resetForNextRequest()` — the security-critical part

`itsResponse` is currently reused for the lifetime of the object. With multiple
requests per connection it **must be fully replaced** each iteration, or headers
and content from response N-1 leak into response N. Reset:

| Member | Action | Where |
|---|---|---|
| `itsResponse` | **reallocate** a fresh `HTTP::Response` | `Connection.h:120` |
| `itsRequest`  | **reallocate** a fresh `HTTP::Request`  | `Connection.h:117` |
| `itsBuffer` | `clear()` | `Connection.h:114` |
| `itsReceivedBytes` | `0` | `Connection.h:123` |
| `itsResponseString` | `clear()` | `Connection.h:126` |
| `itsSentBytes` | `0` | `AsyncConnection.h:342` |
| `itsTotalStreamedBytes` | `0` | `AsyncConnection.h:346` |
| `itsQueryIsFast`, `itsAdminQuery` | `false` | `Connection.h:147,150` |
| `itsFinalStatus` | default-construct | `Connection.h:156` |
| `itsPrematurelyDisconnected` | `false` | `AsyncConnection.h:352` |

Reallocating (rather than assigning into) `itsRequest`/`itsResponse` is the
simplest way to guarantee no residual state; the objects are cheap to construct.

### 4. Idle timeout and resource guards

Today `itsTimeout` is a *request* timer armed in `start()` and cancelled in
`handleCompletedRead` (`AsyncConnection.cpp:392`). Keep-alive adds, mirroring
nginx semantics:

- **`keepalive_timeout`** — a (shorter) idle timer armed *while waiting for the
  next request* on a reused connection. Bounds how long an idle connection holds
  a file descriptor. On expiry, close the connection cleanly.
- **`keepalive_max_requests`** — force `Connection: close` after N exchanges
  (default ~1000), to bound per-connection resource accumulation.
- **max open connections** — the real operational shift. Today
  open-connections ≈ in-flight-requests; with keep-alive the count decouples and
  tracks the number of *clients/pool slots*. Add a ceiling (or document the
  reliance on OS FD limits) so a large pool cannot exhaust descriptors.

All new knobs live in `Spine::Options` / `Server` and are read from
`smartmet.conf`. **Default: keep-alive off**, enabled per deployment once
validated.

### 5. Shutdown interaction

`handleRead` already checks `isShutdownRequested()` at the top
(`AsyncConnection.cpp:172`). The loop must additionally: refuse to re-arm the
read when shutdown is requested, and set `Connection: close` on the final
in-flight response so the gateway drains the pool gracefully during SIGTERM.

## Configuration options

Keep-alive introduces four operator knobs. They live in `Spine::Options`
(`spine/spine/Options.h`) and are parsed in `Options::parseConfig()` /
`parseOptions()` alongside the existing ones, exactly like the `throttle` /
`dns` / `encryption` groups. The connection reads them at response time via
`itsReactor.getOptions().keepalive` — the same path `setServerHeaders()`
already uses for `staleWhileRevalidate` / `staleIfError` — so nothing new needs
to be plumbed through the `AsyncConnection` constructor. (The per-connection
`itsKeepAlive` decision and the `itsRequestCount` counter from the sketch below
remain genuine connection state; the values above are read-only config.)

### The knobs

| Config path | CLI flag | Default | Effect on the algorithm |
|---|---|---|---|
| `keepalive.enabled` | `--keepalive` | `false` | Master switch. When `false`, `decideKeepAlive()` short-circuits to `false` and every connection behaves exactly as today (one request, then close). Ships **off**; enabled per deployment after validation. |
| `keepalive.timeout` | `--keepalivetimeout` | `5` | Seconds an idle reused connection is allowed to wait for the *next* request before the server closes it. Armed by `awaitNextRequest()`; bounds idle FD hold. Advertised to the client in the `Keep-Alive: timeout=N` header. Should be **≤** the gateway's own upstream idle timeout to avoid races (see below). |
| `keepalive.maxrequests` | `--keepalivemaxrequests` | `1000` | Max exchanges served on one connection before `decideKeepAlive()` forces `Connection: close`. `0` = unlimited. Bounds per-connection resource accumulation (mirrors nginx `keepalive_requests`). |
| `keepalive.maxconnections` | `--keepalivemaxconnections` | `0` | Ceiling on simultaneously-open connections. `0` = rely on the OS FD limit. This is the guard for the operational shift noted in §4: with keep-alive, open-connection count tracks the number of client/pool slots rather than in-flight requests. |

### `Options.h` — struct

Mirrors `ThrottleOptions` (`spine/spine/Options.h:30`):

```cpp
struct KeepAliveOptions
{
  bool         enabled        = false;  // master switch, default off
  unsigned int timeout        = 5;      // idle seconds between requests on a reused connection
  unsigned int maxrequests    = 1000;   // exchanges per connection before forced close; 0 = unlimited
  unsigned int maxconnections = 0;      // ceiling on open connections; 0 = OS FD limit
};

struct Options
{
  // ... existing fields ...
  ThrottleOptions   throttle;
  KeepAliveOptions  keepalive;   // <-- new
  // ...
};
```

### `Options.cpp` — libconfig parsing

Added to `parseConfig()` next to the `activerequests.*` block
(`spine/spine/Options.cpp:332`):

```cpp
lookupHostSetting(itsConfig, keepalive.enabled,        "keepalive.enabled");
lookupHostSetting(itsConfig, keepalive.timeout,        "keepalive.timeout");
lookupHostSetting(itsConfig, keepalive.maxrequests,    "keepalive.maxrequests");
lookupHostSetting(itsConfig, keepalive.maxconnections, "keepalive.maxconnections");
```

Optional CLI flags in `parseOptions()` (mirroring the `throttle` flags at
`spine/spine/Options.cpp:175`):

```cpp
("keepalive",            po::value(&keepalive.enabled)->default_value(keepalive.enabled),
    "enable HTTP/1.1 keep-alive (persistent connections)")
("keepalivetimeout",     po::value(&keepalive.timeout)->default_value(keepalive.timeout),
    "idle seconds a reused connection waits for the next request")
("keepalivemaxrequests", po::value(&keepalive.maxrequests)->default_value(keepalive.maxrequests),
    "max requests per connection before forced close (0 = unlimited)")
("keepalivemaxconnections", po::value(&keepalive.maxconnections)->default_value(keepalive.maxconnections),
    "max simultaneously open connections (0 = OS FD limit)");
```

Sanity clamp (in the spirit of the existing throttle checks at
`spine/spine/Options.cpp:225`): if `keepalive.enabled && keepalive.timeout == 0`,
force `timeout = 1` (a 0 idle timeout would close reused connections instantly
and defeat the feature).

### `smartmet.conf` — libconfig block

Added alongside the existing `encryption:` / `*pool:` groups in
`cnf/smartmet.conf.sample`:

```libconfig
// HTTP/1.1 keep-alive (persistent connections).
// Lets an upstream connection pool (APISIX / F5 / nginx) reuse connections and
// skip per-request TCP + TLS setup. Default off.
keepalive:
{
  enabled        = false;  // master switch
  timeout        = 5;      // idle seconds between requests; keep <= gateway upstream idle timeout
  maxrequests    = 1000;   // exchanges per connection before close; 0 = unlimited
  maxconnections = 0;      // 0 = rely on OS file-descriptor limit
};
```

### Tuning notes

- **`timeout` vs. the gateway.** Set `keepalive.timeout` **≥** the gateway's
  upstream keep-alive idle timeout is the wrong way round — set it *equal or
  slightly higher* so the **gateway** decides when to retire an idle pooled
  connection, not the server. If the server closes first while the gateway still
  believes the connection is usable, the gateway races into a closed socket and
  must retry (adds latency and error-log noise). Err on the side of the server
  timing out later than the gateway.
- **`maxrequests` and FD pressure interact.** A high `maxrequests` with a large
  pool and a long `timeout` maximises reuse but also maximises the number of
  long-lived open FDs; `maxconnections` (or the OS limit) is the backstop.
- **All defaults are conservative and the master switch is off**, so merging the
  feature changes nothing until an operator opts in per host.

## Non-goals

- **Pipelining.** `HTTP::parseRequest(const std::string&)` (`HTTP.h:812`)
  returns only `(ParsingStatus, Request)` — it does **not** report how many
  bytes it consumed — so trailing bytes of a pipelined second request cannot be
  cleanly preserved without a parser API change. The upstream-pool use case does
  not pipeline, so this design assumes the buffer is fully consumed per request.
  (Robustness edge: a client that pre-sends the next request's bytes is not
  supported; can be revisited if a parser bytes-consumed count is added.)
- **Gateway (frontend proxy) keep-alive.** `startGatewayReply`
  (`AsyncConnection.cpp:475`) streams a backend response delimited by EOF, and
  the frontend injects `Connection: close` outbound anyway (`Proxy.cpp:203`).
  Client↔frontend keep-alive for gateway responses is a separate, harder step
  and is excluded from the first iteration. Regular, known-length-stream, and
  chunked responses (i.e. backend-served content) are in scope.
- **HTTP/2.** Out of scope; see the design history. Keep-alive is the
  cheap win that serves the actual gateway topology.

## Semantics to verify

- The `Connection`-destructor connection-finished hooks
  (`callClientConnectionFinishedHooks`, `Connection.cpp:60`) currently fire 1:1
  with a request. With keep-alive they fire once per *connection* (many
  requests). Access logging is per-request in `HandlerView::handle`, so it is
  unaffected, but confirm nothing else depends on the 1:1 hook. If per-request
  finished-hooks are required, move that call into `resetForNextRequest()`.

## What does not change

The Spirit request parser, the reactor / handler dispatch, the three-pool
threading model, and every engine and plugin (they only fill `Request` /
`Response`). The change is confined to `AsyncConnection`, `setServerHeaders`,
and a few `Options`/config fields.

## Risks

1. **State leak across requests** — the highest-severity risk. Incomplete reset
   in `resetForNextRequest()` would serve one client's data on another request
   over the same connection. Mitigated by reallocating `itsRequest`/`itsResponse`
   and by an adversarial test (below).
2. **FD / connection exhaustion** — without `keepalive_timeout` and the
   max-open-connections ceiling, a large or misbehaving pool holds descriptors
   open. Mitigated by the guards in §4.
3. **Response desync** — a wrong `Content-Length` is currently masked because
   the connection closes and the client reads to EOF. With keep-alive it becomes
   a fatal framing error (the next response is misread). This surfaces latent
   Content-Length bugs; the delimitability guard in §1 is the backstop.

## Testing

- `curl` with connection reuse (multiple URLs on one invocation) — verify a
  single TCP/TLS connection carries several requests.
- `wrk` / `ab -k` — throughput and correctness under keep-alive load; watch open
  FD count stays bounded.
- **Adversarial reset test:** issue request A (large, distinctive body) then
  request B (small) on the *same* connection and assert response B contains no
  bytes of response A's headers or body.
- Idle-timeout test: open a connection, send one request, go idle, assert the
  server closes after `keepalive_timeout`.
- Shutdown test: SIGTERM mid-pool and assert in-flight requests complete and the
  connection is closed with `Connection: close`.

---

## Implementation sketch

Illustrative only — against the real `AsyncConnection` / `Connection` members.
Not compiled; error handling and the SSL/plain branch duplication (already
present throughout `AsyncConnection.cpp`) are elided for clarity.

### New / changed members

```cpp
// Connection.h (protected) — new keep-alive state and config
std::size_t itsRequestCount = 0;   // exchanges served on this connection
bool        itsKeepAlive    = false;  // decision for the current response

// From Options / Server (read from smartmet.conf):
bool        itsKeepAliveEnabled     = false;  // master switch, default off
long        itsKeepAliveTimeout     = 5;      // idle seconds between requests
std::size_t itsKeepAliveMaxRequests = 1000;   // 0 = unlimited
```

### The connection-token decision (replaces AsyncConnection.cpp:1084-1088)

```cpp
bool AsyncConnection::decideKeepAlive() const
{
  if (!itsKeepAliveEnabled)
    return false;
  if (itsServer->isShutdownRequested())
    return false;
  if (itsKeepAliveMaxRequests > 0 && itsRequestCount + 1 >= itsKeepAliveMaxRequests)
    return false;

  // Gateway responses are framed by EOF -> must close (first iteration).
  if (itsResponse->isGatewayResponse)
    return false;

  // Must be delimitable: known Content-Length or chunked.
  const bool delimitable =
      itsResponse->hasHeader("Content-Length") || itsResponse->getChunked();
  if (!delimitable)
    return false;

  // Honour the client's wishes / HTTP version defaults.
  auto conn = itsRequest->getHeader("Connection");  // lower-cased compare
  const std::string ver = itsRequest->getVersion();
  if (conn && Fmi::ascii_tolower_copy(*conn) == "close")
    return false;
  if (ver == "1.0" && !(conn && Fmi::ascii_tolower_copy(*conn) == "keep-alive"))
    return false;  // 1.0 defaults to close

  return true;  // 1.1 default, or explicit keep-alive
}

void AsyncConnection::setServerHeaders()
{
  itsResponse->setHeader("Server", "SmartMet Server (" __TIME__ " " __DATE__ ")");
  itsResponse->setHeader("Vary", "Accept-Encoding");
  itsResponse->setHeader("Date", makeDateString());

  itsKeepAlive = decideKeepAlive();
  if (itsKeepAlive)
  {
    itsResponse->setHeader("Connection", "keep-alive");
    itsResponse->setHeader("Keep-Alive",
                           "timeout=" + std::to_string(itsKeepAliveTimeout));
  }
  else
  {
    itsResponse->setHeader("Connection", "close");
  }

  // ... existing stale-while-revalidate / stale-if-error logic unchanged ...
}
```

### Reset and loop (called from the terminal write handlers)

```cpp
// Fully clear per-request state so nothing bleeds into the next response.
void AsyncConnection::resetForNextRequest()
{
  ++itsRequestCount;

  // Reallocate rather than assign-into: guarantees no residual header/content.
  itsRequest  = std::make_unique<SmartMet::Spine::HTTP::Request>();
  itsResponse = std::make_unique<SmartMet::Spine::HTTP::Response>();

  itsBuffer.clear();
  itsReceivedBytes      = 0;
  itsResponseString.clear();
  itsSentBytes          = 0;
  itsTotalStreamedBytes = 0;
  itsQueryIsFast        = false;
  itsAdminQuery         = false;
  itsPrematurelyDisconnected = false;
  itsFinalStatus        = boost::system::error_code();
}

// Arm an idle timer and wait for the next request on the same socket.
void AsyncConnection::awaitNextRequest()
{
  itsTimeoutTimer = std::make_unique<DeadlineTimer>(
      itsIoService, std::chrono::seconds(itsKeepAliveTimeout));
  itsTimeoutTimer->async_wait(
      [me = shared_from_this()](const boost::system::error_code& err)
      { me->handleTimer(err); });  // fires request_timeout / closes on idle

  auto onRead = [me = shared_from_this()](const boost::system::error_code& err,
                                          std::size_t n)
  { me->handleRead(err, n); };

  if (itsEncryptionEnabled)
    itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer), onRead);
  else
    socket().async_read_some(boost::asio::buffer(itsSocketBuffer), onRead);
}
```

### Terminal handler: loop instead of drop (writeRegularReply, ~AsyncConnection.cpp:977)

```cpp
// ... inside writeRegularReply, once itsSentBytes >= itsResponseString.size():

// Everything sent. Previously: return -> shared_ptr drops -> destructor closes.
if (itsKeepAlive && !itsFinalStatus)
{
  finalizeStreamLogging();     // fire deferred access-log now, per request
  resetForNextRequest();
  awaitNextRequest();          // keeps `me` alive; loops back into handleRead
  return;
}

// Not keep-alive (or error): fall through to the existing behaviour — return,
// let the shared_ptr drop, and ~AsyncConnection does shutdown()+close().
return;
```

The same three-line pattern (`finalizeStreamLogging` / `resetForNextRequest` /
`awaitNextRequest`) is applied at the clean-completion points of
`writeStreamReply` (`AsyncConnection.cpp:881`) and `finalizeChunkedReply`
(`AsyncConnection.cpp:671`). Stock/error replies (`sendSimpleReply`) keep
closing as they do today.
