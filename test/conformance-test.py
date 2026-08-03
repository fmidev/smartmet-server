#!/usr/bin/env python3
"""HTTP/1.1 conformance checks for persistent connections (BS-3475).

Driven from a raw socket: the interesting cases are the ones a well-behaved
client will not produce -- a missing Host header, a withheld request body, a
bodyless status code, a request pipelined into the same TCP segment.

The server under test is started by conformance-test.sh with no engines or
plugins loaded, so every path here is served either by a stock reply or by one
of the Reactor's built-in admin handlers.
"""

import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8079
HOST = "127.0.0.1"

failures = []


def exchange(chunks, read_rounds=1, pause=0.35):
    """Send each chunk in turn, collecting whatever comes back after each.

    Returns (list_of_raw_responses, closed) where closed says whether the
    server had shut the connection down by the end.
    """
    s = socket.create_connection((HOST, PORT), timeout=5)
    replies = []
    try:
        for chunk in chunks:
            s.sendall(chunk)
            time.sleep(pause)
            s.settimeout(1.0)
            try:
                data = s.recv(65536)
            except socket.timeout:
                data = b""
            replies.append(data)

        closed = False
        for _ in range(read_rounds):
            s.settimeout(0.6)
            try:
                extra = s.recv(65536)
            except socket.timeout:
                break
            if extra == b"":
                closed = True
                break
            replies.append(extra)
    finally:
        s.close()
    return replies, closed


def head_of(raw):
    return raw.decode("latin-1").split("\r\n\r\n")[0]


def status_of(raw):
    text = head_of(raw)
    return text.split("\r\n")[0] if text else ""


def headers_of(raw):
    lines = head_of(raw).split("\r\n")[1:]
    out = {}
    for line in lines:
        if ":" in line:
            name, _, value = line.partition(":")
            out[name.strip().lower()] = value.strip()
    return out


def check(name, condition, detail=""):
    if condition:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("\n          " + detail) if detail else ""))
        failures.append(name)


print("Message framing")

# A response must be delimited by Content-Length or chunked, never both, and a
# reused connection proves the client could find the end of the body.
replies, _ = exchange([b"GET /admin?what=servicestats HTTP/1.1\r\nHost: h\r\n\r\n"] * 2)
hdrs = headers_of(replies[0])
check("200 response carries Content-Length",
      "content-length" in hdrs, repr(head_of(replies[0])))
check("response is not framed twice",
      not ("content-length" in hdrs and "transfer-encoding" in hdrs))
check("connection is reused for the second request",
      status_of(replies[1]).startswith("HTTP/1.1 200"),
      "second reply: %r" % replies[1][:80])

# 204 is framed by the status code: no body, and no Content-Length claiming one.
replies, _ = exchange([b"OPTIONS * HTTP/1.1\r\nHost: h\r\n\r\n"])
hdrs = headers_of(replies[0])
check("204 has no Content-Length",
      status_of(replies[0]).startswith("HTTP/1.1 204") and "content-length" not in hdrs,
      repr(head_of(replies[0])))
check("204 has no body",
      replies[0].endswith(b"\r\n\r\n"), repr(replies[0][-40:]))

# A pipelined follow-up request must not corrupt the first answer.  Answering
# both is a Spine parser change; until then the connection must close so the
# client retries what it got no answer to, rather than hanging.
replies, closed = exchange(
    [b"GET /a HTTP/1.1\r\nHost: h\r\n\r\nGET /b HTTP/1.1\r\nHost: h\r\n\r\n"])
check("pipelined request does not corrupt the response",
      status_of(replies[0]).startswith("HTTP/1.1 404"), repr(replies[0][:80]))
check("pipelined request closes the connection rather than hanging",
      closed or "close" in headers_of(replies[0]).get("connection", ""),
      repr(head_of(replies[0])))


print()
print("HTTP/1.1 correctness")

# Persistent by default for 1.1: no Connection header is needed when keeping.
replies, _ = exchange([b"GET /x HTTP/1.1\r\nHost: h\r\n\r\n"])
check("HTTP/1.1 keeps the connection without announcing it",
      "connection" not in headers_of(replies[0]), repr(head_of(replies[0])))

# The client opts out with Connection: close, in a comma separated token list.
replies, closed = exchange([b"GET /x HTTP/1.1\r\nHost: h\r\nConnection: TE, close\r\n\r\n"])
check("Connection: close is honoured (token list)",
      headers_of(replies[0]).get("connection") == "close" and closed,
      repr(head_of(replies[0])))

# HTTP/1.0 is non-persistent unless the client asks, and the server confirms.
replies, closed = exchange([b"GET /x HTTP/1.0\r\n\r\n"])
check("HTTP/1.0 defaults to close",
      status_of(replies[0]).startswith("HTTP/1.0") and
      headers_of(replies[0]).get("connection") == "close" and closed,
      repr(head_of(replies[0])))

replies, _ = exchange([b"GET /x HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"])
hdrs = headers_of(replies[0])
check("HTTP/1.0 keep-alive is confirmed by the server",
      hdrs.get("connection") == "keep-alive" and "keep-alive" in hdrs,
      repr(head_of(replies[0])))

# Missing Host on 1.1 cannot be routed (RFC 9112 3.2).
replies, closed = exchange([b"GET /admin?what=servicestats HTTP/1.1\r\n\r\n"])
check("missing Host on HTTP/1.1 gives 400",
      status_of(replies[0]).startswith("HTTP/1.1 400"), repr(head_of(replies[0])))
check("missing Host closes the connection",
      closed or headers_of(replies[0]).get("connection") == "close")

# Expect: 100-continue -- the client withholds the body until told to send it.
replies, _ = exchange(
    [b"POST /x HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n",
     b"hello"])
check("Expect: 100-continue gets an interim 100",
      replies[0].startswith(b"HTTP/1.1 100"), repr(replies[0][:60]))
check("the final response follows the body",
      status_of(replies[1]).startswith("HTTP/1.1 404"), repr(replies[1][:80]))

# An HTTP/1.0 client must never be sent an interim response: it has no notion
# of one and would read the 100 as the final answer.
replies, _ = exchange([b"POST /x HTTP/1.0\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\nhello"])
check("HTTP/1.0 gets no interim response",
      not replies[0].startswith(b"HTTP/1.0 100") and
      not replies[0].startswith(b"HTTP/1.1 100"), repr(replies[0][:60]))

# Every response carries a Date and a status line in the client's version.
replies, _ = exchange([b"GET /x HTTP/1.1\r\nHost: h\r\n\r\n"])
check("response has a Date header", "date" in headers_of(replies[0]))
check("status line uses the request's version",
      status_of(replies[0]).startswith("HTTP/1.1 "), repr(status_of(replies[0])))


print()
print("Robustness")

# Hop-by-hop headers describe this connection only and must not reach a
# handler.  Visible here because the dumped request would otherwise show them.
replies, _ = exchange([b"GET /x HTTP/1.1\r\nHost: h\r\nConnection: keep-alive\r\n\r\n"])
check("request with hop-by-hop headers is still served",
      status_of(replies[0]).startswith("HTTP/1.1 404"), repr(replies[0][:80]))

# A malformed request leaves the stream in an untrustworthy state.
replies, closed = exchange([b"NOT-HTTP AT ALL\r\n\r\n"])
check("malformed request gives a framed 400",
      "400" in status_of(replies[0]) and "content-length" in headers_of(replies[0]),
      repr(head_of(replies[0])))
check("malformed request closes the connection", closed)

# Obsolete line folding is a smuggling vector and must be rejected.
replies, closed = exchange([b"GET /x HTTP/1.1\r\nHost: h\r\nX-Foo: a\r\n  b\r\n\r\n"])
check("obsolete line folding is rejected",
      "400" in status_of(replies[0]), repr(head_of(replies[0])))

# An idle persistent connection must be dropped silently, never with a 408 that
# could collide with a request the client is writing at that moment.
print("  ....  (idle timeout is covered by startup-test.sh timings, not here)")


print()
if failures:
    print("%d check(s) FAILED: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)

print("All conformance checks passed")
sys.exit(0)
