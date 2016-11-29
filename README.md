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

![](https://github.com/fmidev/smartmet-server/blob/master/SmartMet_Structure.png "Server structure")

## Licence
The server is published with MIT-license. See [license](../blob/master/LICENCE)

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
