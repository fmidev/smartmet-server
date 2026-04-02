# SmartMet Server

SmartMet Server is a high-performance, high-availability data and product server for MetOcean data, developed and operated by the [Finnish Meteorological Institute (FMI)](https://www.fmi.fi/). Written in C++, it has been in operational use since 2008 and powers the [FMI Open Data Portal](https://en.ilmatieteenlaitos.fi/open-data) since 2013. The server is INSPIRE compliant.

## Capabilities

**Input formats:**
- GRIB 1 and GRIB 2
- NetCDF
- SQL databases
- QueryData (FMI native format)

**Output interfaces and formats:**
- OGC WMS 1.3.0, WFS 2.0
- OGC API — Environmental Data Retrieval (EDR)
- JSON, XML, ASCII, HTML
- GRIB 1, GRIB 2, NetCDF
- Raster images (PNG, JPEG, SVG)

The server specializes in on-demand extraction and product generation from gridded weather data (GRIB, NetCDF). It is designed for multi-core use, provides LRU caching, and supports frontend/backend load balancing via the Sputnik engine.

## Architecture

![SmartMet Server Structure](SmartMet_Structure.png)

The server daemon loads **engines** (shared stateful modules) and **plugins** (HTTP interface handlers) at startup. All components share common **libraries**.

## Components

### Libraries
| Repository | Description |
|---|---|
| [smartmet-library-spine](https://github.com/fmidev/smartmet-library-spine) | Core server framework: HTTP handling, plugin/engine management, configuration, thread pool |
| [smartmet-library-newbase](https://github.com/fmidev/smartmet-library-newbase) | Core QueryData format library — the native FMI weather data format with projection and parameter support |
| [smartmet-library-macgyver](https://github.com/fmidev/smartmet-library-macgyver) | General utilities: astronomy, caching, datetime parsing, filesystem, charset conversion, CSV, Base64 |
| [smartmet-library-gis](https://github.com/fmidev/smartmet-library-gis) | GIS operations: coordinate projections, geometry clipping, antimeridian handling, DEM/raster data, PostGIS |
| [smartmet-library-giza](https://github.com/fmidev/smartmet-library-giza) | Color mapping and SVG rendering: palettes, color trees, color-mapped image generation |
| [smartmet-library-locus](https://github.com/fmidev/smartmet-library-locus) | Geographic name and location lookup: geocoding queries with multilingual support |
| [smartmet-library-imagine](https://github.com/fmidev/smartmet-library-imagine) | 2D graphics rendering: Bezier curves, affine transforms, color blending, image compositing |
| [smartmet-library-imagine2](https://github.com/fmidev/smartmet-library-imagine2) | Updated 2D graphics rendering library (successor to imagine) |
| [smartmet-library-calculator](https://github.com/fmidev/smartmet-library-calculator) | Time series and area calculation tools for QueryData |
| [smartmet-library-delfoi](https://github.com/fmidev/smartmet-library-delfoi) | Oracle database access layer for meteorological observations and flash data |
| [smartmet-library-grid-content](https://github.com/fmidev/smartmet-library-grid-content) | Grid support: Content Server, Data Server, and Query Server APIs with Redis, cache, CORBA, and HTTP implementations |
| [smartmet-library-grid-files](https://github.com/fmidev/smartmet-library-grid-files) | Unified driver layer for grid file formats: GRIB 1, GRIB 2, NetCDF, QueryData |
| [smartmet-library-regression](https://github.com/fmidev/smartmet-library-regression) | Regression testing framework for SmartMet tools and libraries |
| [smartmet-library-smarttools](https://github.com/fmidev/smartmet-library-smarttools) | Scripting tools for the SmartMet editor, also used by qdtools |
| [smartmet-library-textgen](https://github.com/fmidev/smartmet-library-textgen) | Algorithms for generating weather forecast text from querydata |
| [smartmet-library-timeseries](https://github.com/fmidev/smartmet-library-timeseries) | Time series data structures and operations |
| [smartmet-library-trajectory](https://github.com/fmidev/smartmet-library-trajectory) | Trajectory calculations for massless particles |
| [smartmet-library-trax](https://github.com/fmidev/smartmet-library-trax) | High-performance marching-squares contouring: isobands and isolines from 2D gridded data |
| [smartmet-library-woml](https://github.com/fmidev/smartmet-library-woml) | Weather Object Model (WOML) file reader, used by the frontier renderer |

### Engines
Engines are shared stateful modules loaded by the server daemon. They are shared across all plugins.

| Repository | Description |
|---|---|
| [smartmet-engine-querydata](https://github.com/fmidev/smartmet-engine-querydata) | QueryData file management and access, with spatial/temporal interpolation and DEM correction |
| [smartmet-engine-geonames](https://github.com/fmidev/smartmet-engine-geonames) | Geographic name lookup and geocoding via PostGIS/GeoNames database |
| [smartmet-engine-observation](https://github.com/fmidev/smartmet-engine-observation) | Weather station observation data access |
| [smartmet-engine-contour](https://github.com/fmidev/smartmet-engine-contour) | Isoline and isoband contour generation from gridded data |
| [smartmet-engine-gis](https://github.com/fmidev/smartmet-engine-gis) | GIS data and projection support for plugins |
| [smartmet-engine-sputnik](https://github.com/fmidev/smartmet-engine-sputnik) | Frontend/backend cluster management and UDP-based load balancing |
| [smartmet-engine-grid](https://github.com/fmidev/smartmet-engine-grid) | Grid support engine: access to Content Server, Data Server, and Query Server APIs |
| [smartmet-engine-avi](https://github.com/fmidev/smartmet-engine-avi) | Aviation weather data (METAR, TAF, SIGMET) access |
| [smartmet-engine-authentication](https://github.com/fmidev/smartmet-engine-authentication) | API key authentication and access control |
| [smartmet-engine-osm](https://github.com/fmidev/smartmet-engine-osm) | OpenStreetMap data access |

### Plugins
Plugins handle HTTP requests and provide the server's external interfaces.

| Repository | Description |
|---|---|
| [smartmet-plugin-timeseries](https://github.com/fmidev/smartmet-plugin-timeseries) | Time series data retrieval — weather parameters at locations over time |
| [smartmet-plugin-wms](https://github.com/fmidev/smartmet-plugin-wms) | OGC Web Map Service 1.3.0 — georeferenced weather map images |
| [smartmet-plugin-wfs](https://github.com/fmidev/smartmet-plugin-wfs) | OGC Web Feature Service 2.0 — INSPIRE-compliant feature data |
| [smartmet-plugin-edr](https://github.com/fmidev/smartmet-plugin-edr) | OGC API — Environmental Data Retrieval |
| [smartmet-plugin-download](https://github.com/fmidev/smartmet-plugin-download) | Bulk data download in GRIB, NetCDF, and QueryData formats |
| [smartmet-plugin-frontend](https://github.com/fmidev/smartmet-plugin-frontend) | Load-balancing frontend that distributes requests to backend servers |
| [smartmet-plugin-backend](https://github.com/fmidev/smartmet-plugin-backend) | Backend handler for frontend-routed requests |
| [smartmet-plugin-autocomplete](https://github.com/fmidev/smartmet-plugin-autocomplete) | Location name autocomplete with weather enrichment |
| [smartmet-plugin-meta](https://github.com/fmidev/smartmet-plugin-meta) | Metadata about observations (quality codes, observable properties) |
| [smartmet-plugin-admin](https://github.com/fmidev/smartmet-plugin-admin) | Server administration: cluster info, cache stats, active requests |
| [smartmet-plugin-grid-admin](https://github.com/fmidev/smartmet-plugin-grid-admin) | HTTP interface for grid Content Information Storage administration |
| [smartmet-plugin-grid-gui](https://github.com/fmidev/smartmet-plugin-grid-gui) | Browser-based visualization of grid files and content storage |
| [smartmet-plugin-avi](https://github.com/fmidev/smartmet-plugin-avi) | Aviation weather data (METAR, TAF, SIGMET) interface |
| [smartmet-plugin-cross_section](https://github.com/fmidev/smartmet-plugin-cross_section) | Vertical cross-section product generation |
| [smartmet-plugin-trajectory](https://github.com/fmidev/smartmet-plugin-trajectory) | Particle trajectory calculation interface |
| [smartmet-plugin-textgen](https://github.com/fmidev/smartmet-plugin-textgen) | Weather forecast text generation interface |
| [smartmet-plugin-q3](https://github.com/fmidev/smartmet-plugin-q3) | Lua scripting interface for custom weather data queries |

### Tools and Applications
| Repository | Description |
|---|---|
| [smartmet-qdtools](https://github.com/fmidev/smartmet-qdtools) | Comprehensive suite of QueryData handling, conversion, and inspection tools |
| [smartmet-qdcontour](https://github.com/fmidev/smartmet-qdcontour) | Legacy QueryData contouring and map rendering tool |
| [smartmet-qdcontour2](https://github.com/fmidev/smartmet-qdcontour2) | Updated QueryData contouring and map rendering tool |
| [smartmet-shapetools](https://github.com/fmidev/smartmet-shapetools) | Command-line tools for ESRI shapefile operations |
| [smartmet-tools-grid](https://github.com/fmidev/smartmet-tools-grid) | Grid support server, client, and file inspection programs |
| [smartmet-press](https://github.com/fmidev/smartmet-press) | PostScript and ASCII product generation from QueryData |
| [smartmet-fmitools](https://github.com/fmidev/smartmet-fmitools) | FMI-specific meteorological data manipulation tools |
| [smartmet-textgenapps](https://github.com/fmidev/smartmet-textgenapps) | Weather forecast text generation applications |
| [smartmet-frontier](https://github.com/fmidev/smartmet-frontier) | SVG weather chart renderer from WOML input |
| [smartmet-timezones](https://github.com/fmidev/smartmet-timezones) | Timezone data files: Boost.Date_Time database and packed global shapefile |
| [smartmet-fonts](https://github.com/fmidev/smartmet-fonts) | Weather symbol fonts used by SmartMet rendering tools |
| [smartmet-roadindex](https://github.com/fmidev/smartmet-roadindex) | Road weather index calculations |
| [smartmet-roadmodel](https://github.com/fmidev/smartmet-roadmodel) | Road weather model |
| [smartmet-utils](https://github.com/fmidev/smartmet-utils) | General utility scripts |

## License

MIT — see [LICENSE](LICENSE)

## Contributing

Bug reports and pull requests are welcome on [GitHub](../../issues). For larger contributions, open an issue for discussion first. A CLA is required — contact us for details.

## Contact

- Email: beta@fmi.fi
- GitHub Issues: [issues](../../issues)
- FMI Open Source: https://en.ilmatieteenlaitos.fi/open-source-code
- Presentation: https://www.slideshare.net/tervo/smartmet-server-providing-metocean-data
