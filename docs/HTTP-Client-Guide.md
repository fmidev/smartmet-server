# HTTP client guide for SmartMet Server

This guide is for people and programs that fetch data from a SmartMet Server
installation, such as the FMI open data services. It explains how the server
uses the standard HTTP caching headers, what a well-behaved client should do
with them, why the usual "cache busting" tricks do not work against this
server, and how to use connections, compression and error responses so that
your application is fast and the shared service stays responsive.

The short version:

1. **Store the `ETag` the server sends and send it back in `If-None-Match`.**
   When the data has not changed you get a tiny `304 Not Modified` instead of
   the full response.
2. **Do not ask again before the `Expires` time.** The server tells you when
   new data is expected. Polling earlier only returns the same data.
3. **Do not add random or unrecognised query parameters to defeat caches.**
   The server ignores them, so you receive exactly the same cached response,
   just without the benefit of your own cache.
4. **Reuse the connection.** The server keeps HTTP/1.1 connections open. A
   client that opens a new connection for every request pays a TCP and TLS
   handshake each time for nothing.
5. **Back off on errors.** A `503` means the service is busy. Keep using the
   copy you have and retry later, not immediately.

Following these rules makes your application faster, reduces your bandwidth
and keeps the shared service responsive for everyone.

Some of the values quoted below, such as timeouts, size limits and the
compression settings, are configured per installation. The numbers given are
the defaults and are typical, but an installation may differ.

## Contents

- [The headers the server sends](#the-headers-the-server-sends)
  - [How the Expires time is chosen](#how-the-expires-time-is-chosen)
  - [Serving stale data: `stale-while-revalidate` and `stale-if-error`](#serving-stale-data-stale-while-revalidate-and-stale-if-error)
- [Conditional requests: `If-None-Match`](#conditional-requests-if-none-match)
  - [Compressed responses](#compressed-responses)
  - [API keys](#api-keys)
- [Checking for new data without downloading it](#checking-for-new-data-without-downloading-it)
  - [WMS GetCapabilities](#wms-getcapabilities)
  - [Querydata origin times: `/info?what=qengine`](#querydata-origin-times-infowhatqengine)
  - [Grid producers: `/info?what=gridproducers`](#grid-producers-infowhatgridproducers)
  - [How to poll these](#how-to-poll-these)
  - [HEAD requests](#head-requests)
- [What the server does on its side](#what-the-server-does-on-its-side)
- [Why cache busting does not work](#why-cache-busting-does-not-work)
- [Connections](#connections)
  - [Persistent connections](#persistent-connections)
  - [HTTP/1.0 clients](#http10-clients)
  - [Pipelining](#pipelining)
  - [HTTP/2 and reverse proxies](#http2-and-reverse-proxies)
- [Request limits and error responses](#request-limits-and-error-responses)
  - [When the service is busy](#when-the-service-is-busy)
- [Recommendations by client type](#recommendations-by-client-type)
  - [Browsers and JavaScript](#browsers-and-javascript)
  - [curl](#curl)
  - [Python](#python)
  - [Other languages and tools](#other-languages-and-tools)
- [Checklist](#checklist)
- [References](#references)

## The headers the server sends

The main data-producing endpoints (`timeseries`, `edr`, `wms` with its
WMTS and OGC API Tiles interfaces, and `grid-gui`) include the following
headers in their responses. Endpoints that do not send an `ETag`, such as
`wfs` and `download`, are not cached by the server and cannot be validated
with conditional requests. For them the only rule is not to re-request the
same data unnecessarily.

| Header             | Meaning |
|--------------------|---------|
| `ETag`             | A short opaque string that identifies this exact version of the response. It changes when the data, the configuration or your query changes. A compressed response has its own tag, ending in the name of the coding, such as `+gzip` or `+zstd`. |
| `Expires`          | The time after which the response should be considered stale. Before this time the server has nothing newer to give you. |
| `Cache-Control`    | Standard caching directives. Either `public, max-age=N` (safe to cache for `N` seconds), normally followed by `stale-while-revalidate` and `stale-if-error` (see below), or `no-cache, must-revalidate` (may be cached, but must be revalidated with `If-None-Match` before reuse). |
| `Last-Modified`    | When the data behind the response was last updated. Informational. Use the `ETag` for validation. |
| `Vary`             | Normally `Accept-Encoding`. The compressed and uncompressed forms are different representations. |
| `Content-Encoding` | Present when the body is compressed: `gzip` or `zstd`. |

An example of the headers from a timeseries request:

```
HTTP/1.1 200 OK
Date: Tue, 29 Sep 2026 08:15:02 GMT
Content-Type: application/json
Content-Encoding: gzip
ETag: "3f9a1c7b2d4e-timeseries+gzip"
Cache-Control: public, max-age=60, stale-while-revalidate=60, stale-if-error=86400
Expires: Tue, 29 Sep 2026 08:16:02 GMT
Last-Modified: Tue, 29 Sep 2026 08:15:02 GMT
Vary: Accept-Encoding
```

Note that `Cache-Control: no-cache` does **not** mean "do not cache". It means
"you may keep a copy, but check with the server before using it". The check
is the conditional request described next, and it is cheap.

### How the Expires time is chosen

The value depends on the endpoint and on the data source:

- **Forecast and model data** expire when the next model run is expected to
  arrive. The producer configuration records how often each model is updated.
  Once the next run is overdue, the expiry moves forward in short steps until
  the new data actually lands.
- **Observations** and other frequently changing products typically get a
  short fixed lifetime, often around a minute.
- **Some plugins** are configured with a fixed lifetime for every response,
  or with no lifetime at all. In the latter case you get
  `Cache-Control: no-cache, must-revalidate` and should always revalidate.

Whatever the value, requesting the same resource again before `Expires` cannot
return anything new. The server will serve the very same bytes, most likely
straight from its own cache.

### Serving stale data: `stale-while-revalidate` and `stale-if-error`

Every cacheable response, meaning one with a `max-age`, also carries the two
extension directives from RFC 5861. With the default settings they are
`stale-while-revalidate=60` and `stale-if-error=86400`. They tell a cache what
it may do with a copy whose `max-age` has run out:

- **`stale-while-revalidate=60`**: for up to 60 seconds past expiry, keep
  serving the old copy immediately and revalidate it in the background. The
  application never waits for the network on an expired entry, and the next
  request gets the fresh data.
- **`stale-if-error=86400`**: if the revalidation fails, because the server
  answers with a `5xx` status or cannot be reached at all, keep serving the
  old copy for up to a day.

Browsers and most HTTP caching libraries honour both directives without any
configuration. If you maintain your own cache, implement at least
`stale-if-error`: a weather forecast that is an hour old is far more useful
to your users than an error page, and retrying a busy server in a tight loop
makes the situation worse for everyone. See
[When the service is busy](#when-the-service-is-busy).

## Conditional requests: `If-None-Match`

The `ETag` is the key to efficient polling. Keep it together with the response
body. When you want to check whether the data has changed, repeat the request
with the stored value in an `If-None-Match` header:

```
GET /timeseries?producer=pal_skandinavia&place=Helsinki&param=Temperature HTTP/1.1
Host: opendata.fmi.fi
If-None-Match: "3f9a1c7b2d4e-timeseries"
```

If the data is unchanged the server answers with no body at all:

```
HTTP/1.1 304 Not Modified
ETag: "3f9a1c7b2d4e-timeseries"
Expires: Tue, 29 Sep 2026 08:16:02 GMT
Cache-Control: public, max-age=60, stale-while-revalidate=60, stale-if-error=86400
```

Keep using your stored copy, and note the new `Expires` time. If the data has
changed you get a normal `200 OK` with a new body and a new `ETag`. Store both
and continue.

A `304` costs the server almost nothing and costs you a few hundred bytes. A
full response can be anything from kilobytes to tens of megabytes, and in the
worst case the server has to regenerate it. For products like WMS map tiles or
large time series this is the difference between a service that scales and
one that does not.

Practical rules:

- Send back the `ETag` **exactly** as received, including the surrounding
  double quotes and any `+gzip` or `+zstd` suffix.
- Send `If-None-Match` on **every** repeat request, not only after the
  `Expires` time. You may not know when the data changed.
- Prefer `If-None-Match` over `If-Modified-Since`. The server validates by
  entity tag. A request that carries only `If-Modified-Since` is treated
  as a validation request for the currently cached version and may be
  answered `304` regardless of the date given.
- Treat the `ETag` as opaque. Do not try to parse or compare its contents.
- `If-None-Match: *` matches whatever the server currently has and is
  answered `304` for any existing resource. It is rarely what you want.

### Compressed responses

The server compresses a response when the request carries an `Accept-Encoding`
header that names a coding it offers. The codings offered are `zstd` and
`gzip`, in that order of preference; `br` and `deflate` are not produced, and
asking for them simply gets you an uncompressed body. Quality values are
honoured, so a client that cannot decode zstd can say so explicitly with
`Accept-Encoding: gzip, zstd;q=0`. Very small responses, below about 1 kB by
default, and responses that are already compact, such as PNG and WebP images
and PDF documents, are never compressed. A request parameter `gzip=1` forces
gzip regardless of size; it exists for old clients and should not be used in
new ones.

Each encoding is a separate representation with its own entity tag: the coding
is appended to the tag inside the quotes, so the same data is
`"3f9a1c7b2d4e-timeseries"` uncompressed, `"3f9a1c7b2d4e-timeseries+gzip"`
as gzip and `"3f9a1c7b2d4e-timeseries+zstd"` as zstd. This has two practical
consequences:

- **An `If-None-Match` tag only matches when the request still accepts that
  coding.** A stored `+zstd` tag sent with `Accept-Encoding: gzip`, or with no
  `Accept-Encoding` at all, does not match and gets a full `200` in the
  requested encoding. So always send the same `Accept-Encoding` value for
  repeat requests. Most HTTP libraries do this by default.
- **A `304` names the tag you sent.** If you hold several encodings of the
  same resource and send all their tags, the `ETag` of the `304` tells you
  which one is still current. Caches that store one entry per URL need not
  care.

Sending a consistent `Accept-Encoding` also lets the server's own cache reuse
the same representation for you and for everyone else who asked for it.

### API keys

If you use an API key provided by FMI, send it in the `fmi-apikey` request
header rather than as the `fmi-apikey` query parameter:

```sh
curl -sS -H "fmi-apikey: $APIKEY" "$url"
```

The server accepts both, but the header is the better choice:

- **The URL stays the same for everyone.** Browsers, HTTP libraries and
  proxies store responses by URL. A key in the URL makes a separate cache
  entry for every key, so a shared proxy cannot reuse one user's response
  for another.
- **The key stays out of URLs.** URLs end up in browser history, proxy and
  server access logs, `Referer` headers and copied links, and a key in the
  URL goes wherever the URL goes.

## Checking for new data without downloading it

Many clients download a product just to find out whether anything has changed.
There are cheaper ways to ask that question. Use them first, and fetch the
product only when the answer is yes.

### WMS GetCapabilities

The capabilities document carries the same cache headers as the maps
themselves: an `ETag`, an `Expires` and a `Last-Modified`. The `ETag` is a
hash of the document, so it changes when a layer gains a new time step or
model run, when a layer is added or removed, or when the configuration
changes. Poll it with `If-None-Match` like any other resource, and re-read
the layer list only on a `200 OK`.

A full capabilities document can be large, and its `ETag` changes whenever
*any* layer changes. Restrict the request to the layers you use with the
`NAMESPACE` parameter, so the document stays small and its `ETag` only
changes when your layers change:

```
GET /wms?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetCapabilities&NAMESPACE=fmi:ecmwf HTTP/1.1
Host: opendata.fmi.fi
If-None-Match: "1b7e4d9a"
```

`NAMESPACE` takes either a namespace prefix or a regular expression enclosed
in slashes, matched case-insensitively against the full layer names. The
regex form lets you name exactly the layers you use:

```
NAMESPACE=/fmi:ecmwf:pop:rain|fmi:wwi:pop:snow/
```

`FORMAT=application/json` returns the same information as JSON, which is
easier to compare programmatically than the XML. The `Last-Modified` header
reflects the newest data change across all layers, not only the listed ones,
so use the `ETag` for change detection and `Last-Modified` for information.

Note that the WMTS and OGC API Tiles interfaces build on the same layer
metadata, so a change in the WMS `ETag` also means new tiles.

### Querydata origin times: `/info?what=qengine`

For forecast data served from querydata, the question "is there a new model
run?" is answered directly by the querydata engine's status page. Use the
`producer` option to get only the producer you are interested in, and
`format=json` for a machine-readable answer:

```sh
curl -sS 'https://smartmet.fmi.fi/info?what=qengine&producer=pal_skandinavia&format=json&timeformat=iso'
```

The response has one entry per loaded data file, oldest first. Each entry
includes the file's `OriginTime` (the model run time), `MinTime` and
`MaxTime` (the valid time range) and `LoadTime` (when the server loaded it).
The last entry is the newest run:

```json
[{"Producer":"pal_skandinavia","OriginTime":"20260929T060000",
  "MinTime":"20260929T060000","MaxTime":"20261009T060000",
  "LoadTime":"20260929T083012", ...}]
```

Remember the newest `OriginTime`. When it changes, the products for that
producer have changed. Until then, requesting them again only returns what
you already have. The `producer` value must match the producer name exactly.

Other options: `timeformat` accepts `iso`, `sql` (the default), `xml`,
`epoch`, `timestamp` and `http`. Without `format` the page is meant for
browsers. Omitting `producer` lists every producer, which is much larger and
rarely what a polling client wants.

### Grid producers: `/info?what=gridproducers`

Data served through the grid engine (GRIB and NetCDF sources) is organised
into generations, one per model run. The grid producer listing reports the
newest one:

```sh
curl -sS 'https://smartmet.fmi.fi/info?what=gridproducers&producer=ECG&format=json&timeformat=iso'
```

```json
[{"#":1,"ProducerName":"ECG","ProducerId":1,"Title":"...","Description":"...",
  "NumOfGenerations":4,"NewestGeneration":"20260929T000000",
  "OldestGeneration":"20260928T000000"}]
```

`NewestGeneration` is the analysis time of the latest complete model run.
Poll it the same way as the querydata origin time above. Here the `producer`
match is case-insensitive. The `timeformat` option works as for `qengine`.

### How to poll these

The status pages carry no `ETag` or `Expires`, so the rules above do not
apply to them. They are cheap, but they are not free, and the answer cannot
change faster than the model runs behind it. A sensible pattern is:

1. Read the newest origin time or generation once, and store it.
2. Fetch the products you need with their `ETag`s, as described above.
3. Poll the status page no more often than the data can change. Once an
   hour is plenty for a model that runs four times a day. When the value
   changes, fetch the products with `If-None-Match` and let the `ETag`
   confirm what actually changed.

Do not poll a status page every few seconds on the off chance that data has
arrived. The `Expires` header on the products already tells you when to
expect the next run.

### HEAD requests

`HEAD` is supported for every resource and returns exactly the headers a
`GET` would, without the body. It is a convenient way to read the `ETag`,
`Expires` and `Content-Length` of a product, and it saves the transfer.

It does **not** save the server any work. The request is processed as a
`GET` and the product is generated in full; only the body is left out.
For the time series endpoints in particular the `ETag` is a hash of the
generated content, so a `HEAD` costs the same as a `GET`. To find out whether
something has changed, a conditional `GET` with `If-None-Match` is both
cheaper for the server and gives you the new data in the same round trip
when there is any.

## What the server does on its side

Understanding this explains why the recommendations above work, and why cache
busting does not.

A SmartMet Server installation normally consists of a frontend and several
backends. The frontend keeps a response cache keyed by the `ETag`. For every
request it first asks a backend for the `ETag` only. Backends can usually
compute it without generating the product: for a map layer it is derived from
the data version, the layer configuration and the request parameters, and for
a time series it identifies the generated content. If the frontend already
holds a response with that `ETag`, it is returned immediately without any
further backend work. Only when the `ETag` is new does the backend generate
the product and the frontend store it.

Two consequences follow:

- The **same `ETag` means the same bytes**, wherever the request came from.
  Your `If-None-Match` is compared against exactly this value, and a match is
  answered from the frontend without touching the backend.
- The `ETag` is computed from the **parameters the server understands**.
  Anything else in the URL does not take part.

The frontend's cache holds every encoding of a resource side by side, so a
cached response is served to you in whichever encoding you accept. Responses
served from the cache carry no `Age` header; judge their freshness from
`Date` and `Expires`, which is what browsers do anyway.

## Why cache busting does not work

A common habit from web development is to append a random or changing
parameter to a URL, such as `&_=1695974102` or `&nocache=8f3a2`, to force a
fresh response. Against a SmartMet Server this is pointless and harmful:

- **You do not get fresher data.** Unrecognised parameters are ignored when the
  request is interpreted, so the server computes the same `ETag` and returns
  the same cached response as it would for the clean URL. Data becomes
  available when the `Expires` time says it does, not when you ask.
- **You defeat only your own caches.** Your browser, HTTP library or corporate
  proxy sees a different URL every time and cannot reuse anything. You pay for
  a full download on every request.
- **You lose conditional requests.** With a changing URL there is no stored
  `ETag` to send back, so you never receive a cheap `304`.
- **It wastes shared resources.** Every request still passes through the
  service, is logged and counts against usage limits.

The same applies to other variations that do not change the meaning of the
request, such as reordering parameters or changing letter case in values.
The response is the same, and so is the `ETag`.

If you believe you are getting stale data, look at the `Expires` and
`Last-Modified` headers before anything else. They tell you when the server
expects new data and how old the current data is. If they look wrong, report
the URL and the headers to the service provider. Adding random parameters
will not help.

## Connections

### Persistent connections

The server keeps an HTTP/1.1 connection open after each response, as the
protocol intends. A client that sends its next request on the same connection
skips the TCP handshake and, for `https`, the TLS handshake, which together
cost one to three network round trips. For a small response such as a
`304` or a single time series value the handshakes are the dominant cost, so
reusing the connection can make a polling loop several times faster.

Reuse is automatic in browsers and in most HTTP libraries, provided you let
them keep state between requests: use one `requests.Session`, one
`HttpClient`, one `http.Client` for the lifetime of your program rather than
creating a new one per request. `curl` reuses connections within one
invocation, so pass several URLs to one command rather than running curl in a
shell loop when it matters.

What to expect from the server:

- An idle connection is closed after a **30 second** idle period by default.
  The socket is closed silently, without a response, because the server
  cannot know whether you were about to send a request. A well-behaved
  library treats this as normal and simply opens a new connection; if you
  see an occasional "connection reset" or "remote end closed" on the first
  request after a pause, this is why, and the request is safe to retry.
- A connection serves at most **1000 requests** by default. The last
  response carries `Connection: close`.
- The server sends `Connection: close` when it cannot frame the response
  unambiguously or did not read the request completely, for example after a
  `400`, `408` or `413`. Honour it and reconnect.
- The response is always in the HTTP version of the request.

There is no limit on how many connections one client may hold open, other
than a server-wide cap that protects the process from running out of file
descriptors. A batch client that needs concurrency can open several
connections and use them in parallel.

### HTTP/1.0 clients

HTTP/1.0 has no persistent connections. An HTTP/1.0 client that wants one
must ask with `Connection: keep-alive`; the server then confirms it with the
same header and a `Keep-Alive: timeout=30, max=N` header, and closes the
connection after the response otherwise. Modern libraries speak HTTP/1.1 and
none of this applies to them.

### Pipelining

The server supports HTTP/1.1 pipelining, sending several requests on one
connection without waiting for the responses, and answers them in order. Few
clients use it and browsers have removed it, so it is mentioned only for
completeness. Parallel connections are the practical way to get concurrency.

### HTTP/2 and reverse proxies

SmartMet Server itself speaks HTTP/1.1 and HTTP/1.0 only. This is a
deliberate design choice, not an omission. In a production deployment the
server sits behind a reverse proxy or load balancer, such as APISIX, nginx or
an F5, which terminates TLS and HTTP/2 towards the clients and talks
HTTP/1.1 to the server. A client that connects with HTTP/2 is therefore
talking to that layer, and everything in this guide still applies: the cache
headers, the entity tags and the conditional requests pass through the proxy
unchanged.

Whether HTTP/2 helps you depends on what you are. HTTP/2 was designed for
browsers, which cap the number of simultaneous connections to one host at
around six, so many small requests to one origin have to queue. HTTP/2
removes the queue by multiplexing them over a single connection. SmartMet
Server and the proxies in front of it impose no such per-client limit, so a
program that needs concurrency gets it just as well by opening several
HTTP/1.1 connections. If your library negotiates HTTP/2 automatically, let
it; if it does not, there is nothing to gain by forcing it.

## Request limits and error responses

The server enforces a few limits on the request itself, and reports a handful
of status codes that a client should recognise. The limits are configurable
per installation; the defaults are:

| Status | When | What to do |
|--------|------|------------|
| `304 Not Modified` | Your `If-None-Match` matched. | Use your stored copy. |
| `400 Bad Request` | The request could not be parsed, or an HTTP/1.1 request had no `Host` header. | Fix the request. The connection is closed. |
| `404 Not Found` | No plugin handles the path. | Check the URL. |
| `408 Request Timeout` | The request was not received in full within 60 seconds. | Send requests promptly once the connection is open. The connection is closed. |
| `413 Content Too Large` | The whole request exceeds 128 kB. | Send less. This mostly concerns `POST` bodies. The connection is closed. |
| `431 Request Header Fields Too Large` | The request line and headers exceed 16 kB. | Shorten the URL. A `GET` with a long polygon or a long list of parameters can hit this; use `POST` for such queries. |
| `502 Bad Gateway` | The frontend could not get an answer from any backend. | Treat like `503`. |
| `503 Service Unavailable` | The server is overloaded, still starting up or shutting down. | See below. |

Note that a plugin can return `400` or `404` for reasons of its own, such as
an unknown parameter or producer. Those come with an explanatory body.

### When the service is busy

The server protects itself under load. When too many requests are in
progress, or a request queue is full, it answers `503 Service Unavailable`
straight away instead of accepting more work. Behind a frontend you rarely
see this, because the frontend retries the request on another backend; only
when the whole cluster is busy does the `503`, or a `502` from the frontend,
reach you. Startup is a special case: a server that has just been started
accepts connections before all its plugins have finished initialising and
answers `503` for those plugins until they are ready.

The `503` carries no `Retry-After` header. What a client should do:

1. **Keep using the data you have.** This is exactly what `stale-if-error`
   is for. A cache that honours it needs no special handling at all.
2. **Retry with exponential back-off and jitter.** Wait a few seconds before
   the first retry and double the wait on each further failure, up to a
   minute or so. Add a random fraction so that many clients do not retry in
   lockstep.
3. **Do not retry in parallel.** Opening more connections to a busy server
   only makes it busier and makes your own retries fail as well.

The other status that deserves a retry is a closed connection with no
response at all, which is normally the idle timeout described under
[Persistent connections](#persistent-connections). One immediate retry on a
fresh connection is correct there.

## Recommendations by client type

### Browsers and JavaScript

Browsers implement everything in this guide automatically. Fetching a URL
from a page, an image tag or a map library stores the response with its
`ETag`, reuses it until `Expires`, and revalidates with `If-None-Match`
afterwards. They serve stale copies as the `stale-*` directives allow, reuse
connections, and negotiate compression. You get the correct behaviour by
doing nothing.

Things that break it:

- Appending timestamps or random values to URLs. Do not do this.
- Using `cache: "no-store"` in `fetch()`. Use the default, or `"no-cache"`
  if you insist on revalidating every time. Both keep conditional requests.
- Reloading map layers by recreating them with new URLs. Reuse the same URL
  and let the browser revalidate.

### curl

Inspect the headers of a response:

```sh
curl -sS -D - -o /dev/null 'https://opendata.fmi.fi/timeseries?producer=pal_skandinavia&place=Helsinki&param=Temperature&format=json'
```

Poll with a stored `ETag` and only overwrite the local file on change:

```sh
url='https://opendata.fmi.fi/timeseries?producer=pal_skandinavia&place=Helsinki&param=Temperature&format=json'
etag=$(cat etag.txt 2>/dev/null)
curl -sS --compressed -D headers.txt -o response.new \
     ${etag:+-H "If-None-Match: $etag"} "$url"
if grep -q '^HTTP/[0-9.]* 304' headers.txt; then
  echo 'Not modified'
else
  mv response.new response.json
  grep -i '^etag:' headers.txt | cut -d' ' -f2- | tr -d '\r' > etag.txt
fi
```

Also honour `Expires`: schedule the next poll after it, not on a fixed short
interval.

`--compressed` makes curl send `Accept-Encoding` and decode the response.
Use it consistently, since the `ETag` differs between the compressed and
uncompressed representations. When fetching many URLs, give them all to one
curl invocation so that the connection is reused:

```sh
curl -sS --compressed --parallel --parallel-max 4 \
     -o out1.json "$url1" -o out2.json "$url2" -o out3.json "$url3"
```

For a retry policy use `--retry N`, which backs off exponentially and by
default retries on `5xx` responses and on connections that were closed
without a response.

### Python

The `requests` library does no caching on its own. The simplest fix is a
caching adapter that implements the standard rules, for example
`requests-cache` or `CacheControl`:

```python
import requests_cache

session = requests_cache.CachedSession("smartmet", cache_control=True)
r = session.get(url)          # first call: 200, stored with its ETag
r = session.get(url)          # before Expires: served from the local cache
                              # after Expires: revalidated, 304 -> cached body returned
```

If you prefer to handle it yourself, keep the `ETag` per URL, and use one
`Session` for the whole program so that connections are reused:

```python
import requests

session = requests.Session()
etags, bodies = {}, {}

def fetch(url):
    headers = {}
    if url in etags:
        headers["If-None-Match"] = etags[url]
    r = session.get(url, headers=headers, timeout=60)
    if r.status_code == 304:
        return bodies[url]
    if r.status_code in (502, 503) and url in bodies:
        return bodies[url]     # stale-if-error by hand; back off before the next call
    r.raise_for_status()
    etags[url] = r.headers["ETag"]
    bodies[url] = r.content
    return r.content
```

`requests` sends `Accept-Encoding: gzip, deflate` and decodes gzip for you.
It does not accept zstd unless the `zstandard` package is installed, which
is fine; just keep the setting the same across calls. Remember to also wait
until the `Expires` time before polling again.

### Other languages and tools

- **Java**: `java.net.http.HttpClient` does not cache. Use OkHttp with a
  `Cache`, or Apache HttpClient's caching module, both of which implement
  `ETag` and `Expires` handling. All three reuse connections when you keep
  one client instance.
- **Go**: `net/http` does not cache. Store the `ETag` yourself or use a
  caching transport such as `httpcache`. The default `http.Client` pools
  connections; read and close every response body, or the connection cannot
  be reused.
- **wget**: has no conditional-request support for `ETag`. Use `curl` for
  polling.
- **QGIS, ArcGIS, OpenLayers, Leaflet, MapLibre**: WMS and tile layers are
  cached by the underlying HTTP stack. Keep the layer URL stable and avoid
  adding your own cache-breaking parameters. For time-dependent layers use
  the `time` parameter. It is part of the request and yields a distinct
  `ETag` per time step.

## Checklist

- [ ] Store the `ETag` with every cached response.
- [ ] Send `If-None-Match` with the stored `ETag` on every repeat request.
- [ ] Handle `304 Not Modified` by reusing the stored body.
- [ ] Do not request again before the `Expires` time.
- [ ] Poll a cheap status page (WMS `GetCapabilities` with `NAMESPACE`,
      `/info?what=qengine&producer=...` or `/info?what=gridproducers`)
      before re-fetching products, and no more often than the data updates.
- [ ] Send a consistent `Accept-Encoding` and accept compressed responses.
- [ ] Send an FMI API key in the `fmi-apikey` header, not in the URL.
- [ ] Never append random, timestamp or otherwise unrecognised parameters.
- [ ] Reuse connections: one session or client object for the whole program.
- [ ] On `502` or `503`, keep using the stored copy and retry with
      exponential back-off.
- [ ] Use `POST` for queries whose URL would exceed a few kilobytes.

## References

- RFC 9110, HTTP Semantics, section 8.8 (validators), 12.5.3 (`Accept-Encoding`) and 13 (conditional requests)
- RFC 9111, HTTP Caching
- RFC 9112, HTTP/1.1, section 9 (persistent connections)
- RFC 5861, HTTP Cache-Control Extensions for Stale Content
