# explorer-capture

Makes the stills and the WebP animations of [the data explorer guide](../../data-explorer.md)
(`docs/images/explorer/`) by driving the explorer of a running server in a headless Chrome.

* `capture.js` runs the scenes: `node capture.js [--server URL] [--out DIR] [scene ...]`.
  A still is saved as `DIR/<name>.png`, an animation as the frames and their times in
  `DIR/<name>/`. Without scene names every scene is captured (about 10 minutes).
* `scenes.js` has the scenes: each opens the explorer in a known state (the part of its
  address after `#`) and clicks, types, drags and scrolls with a drawn pointer, since a
  headless browser draws none. Map positions are given as longitudes and latitudes.
* `cdp.js` drives Chrome over the DevTools protocol (Node 22 or newer, no packages needed).
* `assemble.py [--out DIR] [--dest DIR] [name ...]` makes the frames into WebP animations
  timed as recorded (quality 70, at most 10 frames a second, identical frames merged, the
  last frame held for 2.5 s) and scales the stills to 80 %. It needs Python 3 with Pillow.

`node capture.js --probe 'api=%2Ftimeseries&producer=...' [--click 'Run|Table'] [--scroll Y]`
opens one state, saves `probe.png` and lists the controls shown, which helps in writing a
scene.

## The server

The scenes use the test data (`smartmet-test-data`, `smartmet-test-db`) through a server
configured like the frontend plugin's test backend, with the EDR (TimeSeries and EDR),
download (Coverages) plugins and the querydata, grid, geonames, gis and observation engines,
and an `api_catalog` group such as:

```
api_catalog:
{
  title          = "SmartMet test server";
  explorer_dir   = "/usr/share/smartmet/explorer";
  uplot_dir      = "/usr/share/smartmet/uplot";
  explorer_defaults:
  {
    center = [25.0, 62.0];
    zoom = 4;
    clock = "24";
    examples: { place = "Helsinki"; fmisid = "100971,101004"; wmo = "2963,2974"; keyword = "synop_fi"; area = "Uusimaa"; };
    projections = ["EPSG:3067 ETRS-TM35FIN", "EPSG:3035 ETRS89 LAEA Europe"];
  };
};
```

The scenes need these datasets: `pal_skandinavia` and `ecmwf_eurooppa_pinta` (surface
forecasts), `meps_ml` (65 model levels around Helsinki), `ecmwf_eps` (an ensemble as
levels), `opendata` observations of 2013-08-05 at station 101004, and the Coverages
collection `pal_skandinavia`. The window is 1240 × 900 pixels in the light theme.

To make the images:

```
node capture.js --server http://localhost:8080
python3 assemble.py
```
