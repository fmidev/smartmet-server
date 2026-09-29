# HTTP caching guide for SmartMet Server clients

This guide is for people and programs that fetch data from a SmartMet Server
installation, such as the FMI open data services. It explains how the server
uses the standard HTTP caching headers, what a well-behaved client should do
with them, and why the usual "cache busting" tricks do not work against this
server.

The short version:

1. **Store the `ETag` the server sends and send it back in `If-None-Match`.**
   When the data has not changed you get a tiny `304 Not Modified` instead of
   the full response.
2. **Do not ask again before the `Expires` time.** The server tells you when
   new data is expected. Polling earlier only returns the same data.
3. **Do not add random or unrecognised query parameters to defeat caches.**
   The server ignores them, so you receive exactly the same cached response,
   just without the benefit of your own cache.

Following these rules makes your application faster, reduces your bandwidth
and keeps the shared service responsive for everyone.

## The headers the server sends

The main data-producing endpoints (`timeseries`, `edr`, `wms` with its
WMTS and OGC API Tiles interfaces, and `grid-gui`) include the following
headers in their responses. Endpoints that do not send an `ETag`, such as
`wfs` and `download`, are not cached by the server and cannot be validated
with conditional requests. For them the only rule is not to re-request the
same data unnecessarily.

| Header          | Meaning |
|-----------------|---------|
| `ETag`          | A short opaque string that identifies this exact version of the response. It changes when the data, the configuration or your query changes. |
| `Expires`       | The time after which the response should be considered stale. Before this time the server has nothing newer to give you. |
| `Cache-Control` | Standard caching directives. Either `public, max-age=N` (safe to cache for `N` seconds) or `no-cache, must-revalidate` (may be cached, but must be revalidated with `If-None-Match` before reuse). |
| `Last-Modified` | When the data behind the response was last updated. Informational. Use the `ETag` for validation. |
| `Vary`          | Normally `Accept-Encoding`. The compressed and uncompressed forms are different representations. |

An example of the headers from a timeseries request:

```
HTTP/1.1 200 OK
Date: Tue, 29 Sep 2026 08:15:02 GMT
Content-Type: application/json
ETag: "3f9a1c7b2d4e-timeseries"
Cache-Control: public, max-age=60
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
Cache-Control: public, max-age=60
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
  double quotes.
- Send `If-None-Match` on **every** repeat request, not only after the
  `Expires` time. You may not know when the data changed.
- Prefer `If-None-Match` over `If-Modified-Since`. The server validates by
  entity tag. A request that carries only `If-Modified-Since` is treated
  as a validation request for the currently cached version and may be
  answered `304` regardless of the date given.
- Treat the `ETag` as opaque. Do not try to parse or compare its contents.

### Compressed responses

The server compresses responses when the client sends an `Accept-Encoding`
header (for example `gzip`, `br` or `zstd`). Always send the same
`Accept-Encoding` value for repeat requests. Most HTTP libraries do this by
default. It lets both your cache and the server's cache reuse the same
representation.

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

## Recommendations by client type

### Browsers and JavaScript

Browsers implement everything in this guide automatically. Fetching a URL
from a page, an image tag or a map library stores the response with its
`ETag`, reuses it until `Expires`, and revalidates with `If-None-Match`
afterwards. You get the correct behaviour by doing nothing.

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

If you prefer to handle it yourself, keep the `ETag` per URL:

```python
import requests

etags, bodies = {}, {}

def fetch(url):
    headers = {}
    if url in etags:
        headers["If-None-Match"] = etags[url]
    r = requests.get(url, headers=headers, timeout=60)
    if r.status_code == 304:
        return bodies[url]
    r.raise_for_status()
    etags[url] = r.headers["ETag"]
    bodies[url] = r.content
    return r.content
```

Remember to also wait until the `Expires` time before polling again.

### Other languages and tools

- **Java**: `java.net.http.HttpClient` does not cache. Use OkHttp with a
  `Cache`, or Apache HttpClient's caching module, both of which implement
  `ETag` and `Expires` handling.
- **Go**: `net/http` does not cache. Store the `ETag` yourself or use a
  caching transport such as `httpcache`.
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
- [ ] Send a consistent `Accept-Encoding` and accept compressed responses.
- [ ] Send an FMI API key in the `fmi-apikey` header, not in the URL.
- [ ] Never append random, timestamp or otherwise unrecognised parameters.

## References

- RFC 9110, HTTP Semantics, section 8.8 (validators) and 13 (conditional requests)
- RFC 9111, HTTP Caching
