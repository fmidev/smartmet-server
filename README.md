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

![](https://github.com/fmidev/smartmet-server/blob/master/SmartMet_Structure.png "Server structure")

SmartMet Server consists of following components:

<table>
<tr>
<th>Component</th><th>Description</th><th>Source Code</th>
</tr>
<tr valign="top">
<td>qdtools         </td><td>Helper programs to handle underling data          </td><td> https://github.com/fmidev/smartmet-qdtools </td></tr>
<tr valign="top">
<td> Libraries       </td><td>Libraries required to run programs and the server </td><td> https://github.com/fmidev/smartmet-library-spine<br>
     		     				    		     	 		  https://github.com/fmidev/smartmet-library-newbase<br>
											  https://github.com/fmidev/smartmet-library-macgyver<br>
											  https://github.com/fmidev/smartmet-library-gis<br>
											  https://github.com/fmidev/smartmet-library-giza<br>
											  https://github.com/fmidev/smartmet-library-locus<br>
											  https://github.com/fmidev/smartmet-library-regression<br>
											  https://github.com/fmidev/smartmet-library-imagine</td>
</tr
<tr valign="top">
<td>Server          </td><td>The server daemon itself                          </td><td> https://github.com/fmidev/smartmet-server  </td>
</tr>
<tr valign="top">
<td>Engines         </td><td>Common modules with a state                       </td><td> https://github.com/fmidev/smartmet-engine-geonames<br>
											 https://github.com/fmidev/smartmet-engine-sputnik<br>
											 https://github.com/fmidev/smartmet-engine-querydata<br>
											 https://github.com/fmidev/smartmet-engine-observation<br>
											 https://github.com/fmidev/smartmet-engine-contour<br>
											 https://github.com/fmidev/smartmet-engine-gis </td>
</tr>
<tr valign="top">
<td>Plugins         </td><td>Plugins providing interfaces to clients           </td><td> https://github.com/fmidev/smartmet-plugin-timeseries<br>
											  https://github.com/fmidev/smartmet-plugin-meta<br>
											  https://github.com/fmidev/smartmet-plugin-frontend<br>
											  https://github.com/fmidev/smartmet-plugin-wfs<br>
											  https://github.com/fmidev/smartmet-plugin-wms<br>
											  https://github.com/fmidev/smartmet-plugin-autocomplete<br>
											  https://github.com/fmidev/smartmet-plugin-backend<br>
											  https://github.com/fmidev/smartmet-plugin-download<br>
											  https://github.com/fmidev/smartmet-plugin-admin </td>
</tr>
</table>

## Licence
The server is published with MIT-license.

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
