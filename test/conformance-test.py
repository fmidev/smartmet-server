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

# Two requests in one segment must both be answered, in order, over the same
# connection.  This is what parseOneRequest() made possible: it reads a single
# message and reports what it consumed, so the second request is left in the
# buffer instead of being swallowed into the first one's body.
replies, _ = exchange(
    [b"GET /admin?what=servicestats HTTP/1.1\r\nHost: h\r\n\r\n"
     b"GET /nosuchthing HTTP/1.1\r\nHost: h\r\n\r\n"],
    read_rounds=3)
joined = b"".join(replies)
first = joined.find(b"HTTP/1.1 200")
second = joined.find(b"HTTP/1.1 404")
check("pipelined requests are both answered",
      first != -1 and second != -1, repr(joined[:120]))
check("pipelined responses come back in request order",
      first != -1 and second != -1 and first < second,
      "200 at %d, 404 at %d" % (first, second))

# A request body must be consumed in full, or the leftover would be read as the
# next request.
replies, _ = exchange(
    [b"POST /a HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\nhello"
     b"GET /b HTTP/1.1\r\nHost: h\r\n\r\n"],
    read_rounds=3)
check("a request body does not leak into the next request",
      b"".join(replies).count(b"HTTP/1.1 404") == 2, repr(b"".join(replies)[:160]))

# Chunked request bodies are decoded by the parser, so the handler sees an
# ordinary message and the connection stays usable afterwards.
replies, _ = exchange(
    [b"POST /a HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n"
     b"5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n",
     b"GET /b HTTP/1.1\r\nHost: h\r\n\r\n"])
check("chunked request body is accepted",
      status_of(replies[0]).startswith("HTTP/1.1 404"), repr(replies[0][:80]))
check("connection survives a chunked request body",
      status_of(replies[1]).startswith("HTTP/1.1 404"), repr(replies[1][:80]))

# Request smuggling defences (RFC 9112 6.3).
for label, raw in [
    ("Content-Length with Transfer-Encoding",
     b"POST /a HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n"
     b"Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
    ("conflicting Content-Length fields",
     b"POST /a HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!"),
    ("non-numeric Content-Length",
     b"POST /a HTTP/1.1\r\nHost: h\r\nContent-Length: 0x5\r\n\r\nhello"),
    ("undecodable transfer coding",
     b"POST /a HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: gzip\r\n\r\nxx"),
]:
    replies, closed = exchange([raw])
    check("%s is rejected with 400" % label,
          "400" in status_of(replies[0]), repr(head_of(replies[0])))
    check("%s closes the connection" % label,
          closed or headers_of(replies[0]).get("connection") == "close")

# HEAD: headers as for GET, but no content.
replies, _ = exchange([b"HEAD /admin?what=servicestats HTTP/1.1\r\nHost: h\r\n\r\n",
                       b"GET /admin?what=servicestats HTTP/1.1\r\nHost: h\r\n\r\n"])
head_hdrs = headers_of(replies[0])
get_hdrs = headers_of(replies[1])
check("HEAD returns a 200 with no body",
      status_of(replies[0]).startswith("HTTP/1.1 200") and replies[0].endswith(b"\r\n\r\n"),
      repr(replies[0][-60:]))
check("HEAD reports the length GET would have sent",
      head_hdrs.get("content-length") == get_hdrs.get("content-length"),
      "HEAD said %r, GET said %r" % (head_hdrs.get("content-length"),
                                     get_hdrs.get("content-length")))
check("connection survives a HEAD request",
      status_of(replies[1]).startswith("HTTP/1.1 200"), repr(replies[1][:80]))


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

# A client must not be able to hold a connection open by dribbling header bytes
# that never reach the terminating blank line.
replies, closed = exchange([b"GET /a HTTP/1.1\r\nHost: h\r\n" + b"X-Pad: " + b"p" * 40000 + b"\r\n"])
check("an oversized header section gives 431",
      "431" in status_of(replies[0]), repr(head_of(replies[0])))

# An unsupported expectation cannot be met (RFC 9110 10.1.1).
replies, closed = exchange([b"POST /a HTTP/1.1\r\nHost: h\r\nExpect: something-else\r\n"
                            b"Content-Length: 5\r\n\r\n"])
check("an unsupported Expect gives 417",
      "417" in status_of(replies[0]), repr(head_of(replies[0])))

# An idle persistent connection must be dropped silently, never with a 408 that
# could collide with a request the client is writing at that moment.
print("  ....  (idle timeout is covered by startup-test.sh timings, not here)")


print()
if failures:
    print("%d check(s) FAILED: %s" % (len(failures), ", ".join(failures)))
    sys.exit(1)

print("All conformance checks passed")
sys.exit(0)
