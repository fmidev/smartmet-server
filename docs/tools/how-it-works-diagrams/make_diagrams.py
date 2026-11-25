#!/usr/bin/env python3
"""Generate the SVG diagrams of docs/how-smartmet-server-works.md."""
import os
import sys
from xml.sax.saxutils import escape

OUT = sys.argv[1] if len(sys.argv) > 1 else "."

KINDS = {
    "producer": ("#fef3c7", "#d97706"),
    "storage": ("#e2e8f0", "#64748b"),
    "backend": ("#ccfbf1", "#0d9488"),
    "frontend": ("#dbeafe", "#2563eb"),
    "client": ("#ede9fe", "#7c3aed"),
    "cache": ("#fce7f3", "#db2777"),
    "neutral": ("#f8fafc", "#94a3b8"),
    "group": ("#ffffff", "#cbd5e1"),
}

STYLE = """
<style>
  text { font-family: Inter, 'Segoe UI', 'Helvetica Neue', Helvetica, Arial, sans-serif; fill: #1e293b; }
  .t { font-size: 15px; font-weight: 600; }
  .s { font-size: 12.5px; fill: #475569; }
  .m { font-size: 12px; fill: #334155; font-family: 'JetBrains Mono', Menlo, Consolas, monospace; }
  .h { font-size: 12px; font-weight: 700; letter-spacing: 0.08em; fill: #64748b; }
  .g { font-size: 14px; font-weight: 700; fill: #334155; }
  .l { font-size: 12px; fill: #475569; font-style: italic; }
  .ln { stroke: #475569; stroke-width: 1.6; fill: none; }
  .dash { stroke-dasharray: 6 4; }
</style>
<defs>
  <marker id="arrow" viewBox="0 0 10 10" refX="9.5" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
    <path d="M0,0 L10,5 L0,10 z" fill="#475569"/>
  </marker>
</defs>
"""


class Svg:
    def __init__(self, w, h, title):
        self.w, self.h, self.parts = w, h, []
        self.title = title

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, cls="s", anchor="middle"):
        self.add(f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{escape(s)}</text>')

    def box(self, x, y, w, h, kind, title=None, lines=(), cls_lines="s", rx=10, sw=1.5):
        fill, stroke = KINDS[kind]
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
                 f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')
        n = len(lines)
        total = (18 if title else 0) + 16 * n
        ty = y + h / 2 - total / 2 + 13
        if title:
            self.text(x + w / 2, ty, title, "t")
            ty += 19
        for line in lines:
            self.text(x + w / 2, ty, line, cls_lines)
            ty += 16

    def group(self, x, y, w, h, label, kind="group"):
        fill, stroke = KINDS[kind]
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" '
                 f'stroke="{stroke}" stroke-width="1.5" stroke-dasharray="5 4"/>')
        self.text(x + 16, y + 22, label, "g", "start")

    def arrow(self, pts, dashed=False, both=False):
        d = " ".join(f"{x},{y}" for x, y in pts)
        cls = "ln dash" if dashed else "ln"
        start = ' marker-start="url(#arrow)"' if both else ""
        self.add(f'<polyline points="{d}" class="{cls}" marker-end="url(#arrow)"{start}/>')

    def line(self, pts, dashed=False):
        d = " ".join(f"{x},{y}" for x, y in pts)
        cls = "ln dash" if dashed else "ln"
        self.add(f'<polyline points="{d}" class="{cls}"/>')

    def label(self, x, y, s, anchor="middle"):
        # Label on a white background so it stays readable over lines
        w = len(s) * 6.0 + 10
        x0 = {"middle": x - w / 2, "start": x - 5, "end": x - w + 5}[anchor]
        self.add(f'<rect x="{x0:.1f}" y="{y - 12}" width="{w:.1f}" height="16" rx="3" fill="#ffffff"/>')
        self.add(f'<text x="{x}" y="{y}" class="l" text-anchor="{anchor}">{escape(s)}</text>')

    def diamond(self, cx, cy, hw, hh, kind, lines):
        fill, stroke = KINDS[kind]
        pts = f"{cx},{cy-hh} {cx+hw},{cy} {cx},{cy+hh} {cx-hw},{cy}"
        self.add(f'<polygon points="{pts}" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
        ty = cy - 8 * (len(lines) - 1) + 4
        for line in lines:
            self.text(cx, ty, line, "t")
            ty += 17

    def save(self, name):
        body = "\n".join(self.parts)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{escape(self.title)}">\n'
               f'<title>{escape(self.title)}</title>\n{STYLE}\n'
               f'<rect width="{self.w}" height="{self.h}" rx="12" fill="#ffffff"/>\n{body}\n</svg>\n')
        with open(os.path.join(OUT, name), "w") as f:
            f.write(svg)


# ----------------------------------------------------------------------
# 1. The big picture
# ----------------------------------------------------------------------
s = Svg(1000, 350, "From data producers to clients")
for x, w, label in ((20, 190, "DATA PRODUCERS"), (260, 190, "STORAGE"), (500, 310, "SMARTMET SERVER"),
                    (850, 130, "CLIENTS")):
    s.text(x + w / 2, 30, label, "h")
rows = [55, 125, 195, 265]
prod = [("NWP models", ["GRIB, NetCDF"]), ("Post-processing", ["QueryData"]),
        ("Radar, satellite", ["HDF5, GeoTIFF / COG"]), ("Observation network", ["stations, lightning…"])]
for y, (t, l) in zip(rows, prod):
    s.box(20, y, 190, 56, "producer", t, l)
s.box(260, 55, 190, 196, "storage", "Shared file systems",
      ["(NFS)", "", "files are read in place,", "memory-mapped", "", "+ Redis index of grid files"])
s.box(260, 265, 190, 56, "storage", "PostgreSQL", ["observation databases"])
for y in rows[:3]:
    s.arrow([(210, y + 28), (258, y + 28)])
s.arrow([(210, 293), (258, 293)])
s.group(500, 48, 310, 282, "")
s.box(515, 80, 130, 236, "backend", "Backends", ["", "engines hold", "the data,", "plugins compute", "each answer"])
s.box(665, 80, 130, 236, "frontend", "Frontends", ["", "route requests", "to backends,", "cache responses"])
s.arrow([(450, 153), (513, 153)])
s.arrow([(450, 293), (513, 293)])
s.arrow([(645, 198), (663, 198)])
clients = [("Apps", []), ("Web maps", []), ("OGC clients", []), ("Other servers", [])]
for y, (t, l) in zip(rows, clients):
    s.box(850, y, 130, 56, "client", t, l)
    s.arrow([(795, 198), (848, y + 28)])
s.save("overview.svg")

# ----------------------------------------------------------------------
# 2. Anatomy of smartmetd
# ----------------------------------------------------------------------
s = Svg(960, 610, "Inside the smartmetd process")
s.group(20, 16, 920, 576, "smartmetd — one process")
s.box(40, 52, 880, 52, "neutral", "HTTP server", ["sockets, TLS, keep-alive, compression, thread pools (fast, slow, admin)"])
s.box(40, 122, 880, 52, "neutral", "Reactor (spine library)",
      ["loads engines and plugins at startup, routes URLs to plugins, answers /admin and /info"])
s.arrow([(480, 104), (480, 120)])

engines = [("querydata", "QueryData files"), ("grid", "GRIB, NetCDF, QueryData via an index"),
           ("observation", "station observations from databases"), ("geonames", "places, elevation, time zones"),
           ("gis", "projections, PostGIS shapes, DEM"), ("contour", "isolines and isobands"),
           ("sputnik", "cluster discovery"), ("avi", "aviation messages"),
           ("satellite", "satellite imagery (COG)"), ("osm", "map background data"),
           ("authentication", "API keys, access rights")]
plugins = [("timeseries", "tables for points, areas, paths"), ("edr", "OGC API – EDR"),
           ("wms", "maps and tiles: WMS, WMTS, Tiles, Dali"), ("wfs", "OGC WFS stored queries"),
           ("download", "gridded data as GRIB, NetCDF, GeoTIFF"), ("autocomplete", "place-name suggestions"),
           ("avi", "aviation messages"), ("textgen", "forecasts as text"),
           ("cross_section", "vertical cross-sections"), ("trajectory", "air-parcel trajectories"),
           ("q3", "Lua-scripted products"), ("frontend, backend", "clustering")]


def listing(s, x, y, w, h, kind, title, subtitle, items):
    fill, stroke = KINDS[kind]
    s.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
    s.text(x + 18, y + 28, title, "t", "start")
    s.text(x + 18, y + 47, subtitle, "s", "start")
    yy = y + 76
    for name, desc in items:
        s.add(f'<text x="{x + 18}" y="{yy}" class="m" text-anchor="start" font-weight="700">{escape(name)}</text>')
        s.text(x + 160, yy, desc, "s", "start")
        yy += 22


listing(s, 40, 218, 420, 360, "backend", "Engines  (engines/*.so)", "shared data and state, used by the plugins",
        engines)
listing(s, 500, 218, 420, 360, "frontend", "Plugins  (plugins/*.so)", "HTTP request handlers, one or more URLs each",
        plugins)
s.arrow([(710, 174), (710, 216)])
s.label(720, 200, "routes requests", "start")
s.arrow([(250, 174), (250, 216)])
s.label(260, 200, "loads at startup", "start")
s.arrow([(500, 330), (462, 330)])
s.label(481, 318, "use")
s.save("process-anatomy.svg")

# ----------------------------------------------------------------------
# 3. Life of a request
# ----------------------------------------------------------------------
s = Svg(960, 330, "The life of a request on a backend")
steps = [("1  Connection", ["read and validate", "the request,", "keep-alive"]),
         ("2  Routing", ["/timeseries →", "timeseries plugin"]),
         ("3  Scheduling", ["fast or slow pool,", "or refuse at once", "if overloaded"]),
         ("4  Plugin", ["parse, locate,", "choose producer,", "interpolate, format"]),
         ("5  Response", ["ETag, Expires,", "zstd / gzip"]),
         ("6  Logging", ["access log,", "service statistics"])]
for i, (t, l) in enumerate(steps):
    x = 20 + i * 157
    kind = "frontend" if i == 3 else "neutral"
    s.box(x, 40, 140, 100, kind, t, l)
    if i:
        s.arrow([(x - 17, 90), (x - 2, 90)])
eng = [("geonames", ["where is Helsinki?", "elevation, time zone"]),
       ("querydata / grid", ["latest model run,", "values at the point"]),
       ("observation", ["measurements,", "if asked for"])]
for i, (t, l) in enumerate(eng):
    x = 345 + i * 160
    s.box(x, 210, 145, 72, "backend", t, l)
    s.arrow([(561, 140), (561, 175), (x + 72, 175), (x + 72, 208)], dashed=True)
s.text(561, 310, "engines: shared data, already loaded and cached", "s")
s.save("request-life.svg")

# ----------------------------------------------------------------------
# 4. Cluster
# ----------------------------------------------------------------------
s = Svg(960, 430, "A SmartMet Server cluster")
s.box(20, 150, 110, 80, "client", "Clients")
s.box(165, 150, 140, 80, "neutral", "Load balancer", ["(F5 or similar)"])
s.arrow([(130, 190), (163, 190)])
for y, name in ((80, "Frontend 1"), (220, "Frontend 2")):
    s.box(350, y, 190, 90, "frontend", name, ["frontend plugin,", "response cache"])
    s.arrow([(305, 190), (348, y + 45)])
    s.arrow([(540, y + 45), (608, y + 45)])
s.line([(610, 72), (610, 327)])
s.label(610, 62, "HTTP, pooled connections")
backs = [40, 125, 210, 295]
for i, y in enumerate(backs):
    s.box(700, y, 230, 65, "backend", f"Backend {i + 1}", ["engines + data plugins"])
    s.arrow([(610, y + 32), (698, y + 32)])
s.arrow([(445, 312), (445, 385), (815, 385), (815, 362)], dashed=True, both=True)
s.label(630, 380, "UDP discovery every 5 s: “what do you serve, how loaded are you?”")
s.save("cluster.svg")

# ----------------------------------------------------------------------
# 5. QueryData file lifecycle
# ----------------------------------------------------------------------
s = Svg(980, 250, "How a QueryData file becomes available")
s.group(15, 20, 380, 205, "Data producer (rsync)", "group")
s.group(415, 20, 550, 205, "querydata engine (each backend)", "group")
cards = [
    ("producer", "rsync transfers", [".202611251200_pal…", ".sqd.Xk3a9Q", "hidden: ignored"]),
    ("producer", "rename", ["202611251200_pal…", "_pinta.sqd", "complete file"]),
    ("backend", "notice", ["directory rescan,", "at most 60 s", "(refresh_interval)"]),
    ("backend", "map and index", ["by producer and", "origin time;", "now in what=qengine"]),
    ("backend", "drop old runs", ["keep the newest", "number_to_keep", "runs (default 2)"]),
]
xs = [30, 215, 430, 610, 790]
for x, (kind, t, l) in zip(xs, cards):
    s.box(x, 70, 160, 120, kind, t, l, cls_lines="s")
for a, b in zip(xs, xs[1:]):
    s.arrow([(a + 160, 130), (b - 2, 130)])
s.save("querydata-lifecycle.svg")

# ----------------------------------------------------------------------
# 6. Grid engine
# ----------------------------------------------------------------------
s = Svg(1035, 410, "How the grid engine finds and reads data")
s.box(20, 60, 200, 150, "storage", "Model output on NFS",
      ["", "…/MEPS/20261125T12/", "*.grib2", "…/ECMWF/…/*.grib", "…/*.nc"], cls_lines="m")
s.box(260, 60, 180, 150, "producer", "Registration tools", ["", "filesys2smartmet", "radon2smartmet",
                                                             "(separate daemons)"])
s.arrow([(220, 135), (258, 135)])
s.label(239, 127, "scan")
fill, stroke = KINDS["storage"]
s.add(f'<rect x="480" y="40" width="200" height="200" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
s.text(580, 66, "Content index (Redis)", "t")
tree = [("producers", "54", 0), ("generations", "~340", 1), ("files", "~470 000", 2), ("contents", "~14.5 M", 3)]
for i, (n, c, ind) in enumerate(tree):
    y = 100 + i * 32
    if ind:
        x0 = 500 + (ind - 1) * 16 + 4
        s.line([(x0, y - 22), (x0, y - 4), (x0 + 10, y - 4)])
    s.text(500 + ind * 16 + (4 if ind else 0), y, n, "m", "start")
    s.text(664, y, c, "s", "end")
s.text(580, 228, "FMI production counts", "s")
s.arrow([(440, 135), (478, 135)])
s.label(459, 127, "register")
s.group(700, 20, 320, 310, "Grid engine (each backend)")
s.box(715, 50, 290, 44, "frontend", "Plugins", ["timeseries, wms, edr, download…"])
s.box(715, 120, 290, 62, "backend", "Query Server", ["names, mappings, interpolation, Lua"])
s.box(715, 210, 140, 100, "backend", "Content Server", ["in-memory copy", "of the index"])
s.box(865, 210, 140, 100, "backend", "Data Server", ["reads files,", "decompression", "cache"])
s.arrow([(860, 94), (860, 118)])
s.arrow([(810, 182), (785, 208)])
s.arrow([(910, 182), (935, 208)])
s.arrow([(680, 225), (713, 225)])
s.label(697, 216, "events")
s.arrow([(120, 210), (120, 372), (935, 372), (935, 312)])
s.label(520, 366, "values read in place (memory-mapped)")
s.save("grid-engine.svg")

# ----------------------------------------------------------------------
# 7. Observation caches
# ----------------------------------------------------------------------
s = Svg(980, 375, "How the observation engine answers requests")
s.box(20, 40, 190, 70, "storage", "PostgreSQL replica", ["recent data"])
s.box(20, 250, 190, 70, "storage", "PostgreSQL master", ["older data, archive"])
s.box(270, 40, 200, 70, "producer", "Cache update threads", ["one per data kind,", "every few s – min"])
s.arrow([(210, 75), (268, 75)])
s.add(f'<rect x="530" y="20" width="210" height="140" rx="12" fill="#fff7fb" stroke="#db2777" '
      f'stroke-width="1.5" stroke-dasharray="5 4"/>')
s.box(545, 35, 180, 52, "cache", "Disk cache", ["recent days"])
s.box(545, 97, 180, 52, "cache", "Memory cache", ["recent hours"])
s.arrow([(470, 75), (528, 75)])
s.box(800, 200, 160, 70, "client", "Request", ["TA at Kumpula,", "last 24 hours"])
s.diamond(635, 235, 95, 50, "neutral", ["whole period", "in cache?"])
s.arrow([(800, 235), (732, 235)])
s.arrow([(635, 185), (635, 162)])
s.label(652, 178, "yes", "start")
s.line([(540, 235), (500, 235)])
s.label(520, 228, "no")
s.arrow([(500, 235), (500, 285), (212, 285)])
s.label(356, 279, "older data: master")
s.arrow([(500, 235), (500, 180), (115, 180), (115, 112)])
s.label(300, 174, "recent data: replica")
s.text(356, 340, "a request that starts further back than the cache goes to a database,", "s")
s.text(356, 357, "chosen by how far back the request reaches", "s")
s.save("observation-cache.svg")

# ----------------------------------------------------------------------
# 8. WMS pipeline
# ----------------------------------------------------------------------
s = Svg(980, 330, "How a WMS map is drawn")
s.box(20, 40, 190, 80, "client", "Request", ["+ product JSON", "(layers, styles)"])
s.box(250, 40, 170, 80, "neutral", "Hash", ["of product and data", "versions = ETag"])
s.diamond(540, 80, 85, 46, "neutral", ["cached?"])
s.box(700, 40, 260, 80, "cache", "Cached image", ["or 304 Not Modified"])
s.arrow([(210, 80), (248, 80)])
s.arrow([(420, 80), (453, 80)])
s.arrow([(625, 80), (698, 80)])
s.label(660, 72, "yes")
row2 = [("Read data", ["per layer: querydata,", "grid, observation…"], "backend"),
        ("Contour engine", ["isobands, isolines", "in map projection"], "backend"),
        ("SVG document", ["layers through", "a template, CSS"], "frontend"),
        ("Output", ["PNG, WebP, PDF;", "GeoJSON, MVT, GeoTIFF…"], "client")]
xs = [20, 265, 510, 755]
for x, (t, l, k) in zip(xs, row2):
    s.box(x, 210, 205, 84, k, t, l)
for a, b in zip(xs, xs[1:]):
    s.arrow([(a + 205, 252), (b - 2, 252)])
s.arrow([(540, 126), (540, 165), (122, 165), (122, 208)])
s.label(330, 159, "no: render")
s.save("wms-pipeline.svg")
