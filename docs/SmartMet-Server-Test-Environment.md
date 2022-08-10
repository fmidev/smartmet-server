# Test environment  

Module tests are ment to be run against local postgres databases available in docker image **smartmet-server-test-db**; see https://hub.docker.com/r/fmidev/smartmet-server-test-db. Pull and run the image with following command:

`docker run --net host -p 5432:5432 fmidev/smartmet-server-test-db`  

Test configurations expect following directory structure (note: module directories and named using short module names, e.g. _authentication_ instead of _smartmet-engine-authentication_):  

`<basedirectory>` denotes root of module tree; e.g. /smartmet-server

`<basedirectory>`/**engines**  
`<basedirectory>`/**engines**/_authentication_  
`<basedirectory>`/**engines**/_authentication/test_  
`<basedirectory>`/**engines**/_contour_  
..  
`<basedirectory>`/**plugins**  
`<basedirectory>`/**plugins**/_autocomplete_  
`<basedirectory>`/**plugins**/_autocomplete/test_  
`<basedirectory>`/**engines**/_cross_section_  
..  

To create such environment for module testing, one could for example
* clone spine, server, engines and plugins to `<basedirectory>`
* build and install spine, server, engines and plugins
* create directories **engines** and **plugins** in `<basedirectory>`
* using short module names, create symbolic links in engines/plugins directory for each engine/plugin module;  
  e.g. cd `<basedirectory>`engines; ln -s ../smartmet-engine-authentication ./authentication

Module test can be run in module directory by running 'make test'; e.g.  
cd `<basedirectory>`/smartmet-engine-authentication; make test  