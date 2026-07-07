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

The test is not a unit test suite — it is a single integration test (`test/startup-test.sh`) that launches `smartmetd` with an empty engine/plugin config (`test/minimal.conf`), waits for the "Launched Synapse server" log line, sends an HTTP OPTIONS request, then shuts the server down via SIGTERM and verifies clean exit.

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
