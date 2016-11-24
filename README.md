# SmartMet Server
SmartMet Server is a data and procut server for MetOcean data. It provides high capacity and high availability data and product server for MetOcean data. The server is written in C++. 

The server can read input data from various sources:
* GRIB (1 and 2) 
* NetCDF
* SQL database

The server provides several output interfaces:
* WMS 1.3.0
* WFS 2.0
* Several custom interface
and several output formats:
* JSON
* XML
* ASCII
* HTML
* SERIAL
* GRIB1
* GRIB2 
* NetCDF
* Raster images

The server is INSPIRE compliant. It is used for FMI data services and product generation. It's been operative since 2008 and used for FMI Open Data Portal since 2013.

The server is especially good for extracting weather data and generating products based on gridded data (GRIB and NetCDF). The data is extracted and products generating always on-demand. 

## Server Structure
SmartMet Server consists of following components:

| Component       |Description                                       |
|-----------------|--------------------------------------------------|
| qdtools         |Helper programs to handle underling data          | 
| libraries       |Libraries required to run programs and the server |
| server          |The server daemon itself                          |
| engines         |Common modules with a state                       |
| plugins         |Plugins providing interfaces to clients           |

![](/SmartMet_Structure.svg "Server structure)

## Licence
The server is published with MIT-license. See [license](../blob/master/LICENCE)

## How to test
Following steps are required to perform a quick experiment installation:

1. Get RHEL7 or CentOS7 server
2. Clone, compile and install following packages, note that libraries has to be installed before compaling other packages
  * smartmet-library-spine 
  * smartmet-library-newbase 
  * smartmet-library-macgyver
  * smartmet-library-gis
  * smartmet-library-giza
  * smartmet-library-locus
  * smartmet-library-regression
  * smartmet-library-imagine
  * https://github.com/fmidev/smartmet-server
  * https://github.com/fmidev/smartmet-engine-querydata
  * https://github.com/fmidev/smartmet-engine-geo
  * https://github.com/fmidev/smartmet-engine-contour
  * https://github.com/fmidev/smartmet-engine-gis
  * https://github.com/fmidev/smartmet-plugin-admin
  * https://github.com/fmidev/smartmet-plugin-wms

3. Get some data i.e. from `http://data.fmi.fi/fmi-apikey/__your-api-key__/download?producer=hirlam&param=Pressure,Temperature,DewPoint,Humidity,WindUMS,WindVMS,Precipitation1h&bbox=19.1000000138525,59.6999999968758,31.7000000148709,70.0999999955105&levels=0&format=querydata&projection=EPSG:4326`
4. Configure your server (`/etc/smartmet-server.conf`) to contain compiled engines and plugins
5. Configure you data direcotry to querydata engine (`/etc/smarmet-server/engine/querydata.conf`)
6. Configure WMS layer to match your parameters (i.e. temperature) and data in querydata engine configuration (`/etc/smartmet-server/plugins/wms`). See https://github.com/fmidev/smartmet-plugin-wms/wiki/WMS-Tutorial for more details.
7. Start the Server (`systemctl start smartmet-server`)
8. Access your WMS layer i.e in `http://localhost/wms?request=GetMap&service=WMS&layers=fmi:pal:temperature&height=400&width=400&bbox=60,19,65,25&styles=&version=1.3.0&crs=EPSG:4326&format=image/png`

If you are interested in using the server operatively, don't hesitate to contact us! 

## How to contribute
Found a bug? Want to implement a new feature? Your contribution is very welcome!

Small changes and bug fixes can be submitted via pull request. In larger contributions, premilinary plan is recommended (in GitHub wiki). 

CLA is required in order to contribute. Please contact us for more information!

## Documentation
Each module is documented in module [module wiki](../../wiki). 

## Communication and Resources
You may contact us from following channels:
* Email: beta@fmi.fi
* Facebook: https://www.facebook.com/fmibeta/
* GitHub: [issues](../../issues)

Other resources which may be useful:
* Presentation about the server: http://www.slideshare.net/tervo/smartmet-server-providing-metocean-data
* Our public web pages (in Finnish):  http://ilmatieteenlaitos.fi/avoin-lahdekoodi
