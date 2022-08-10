# Tutorial

This tutorial explains how to configure the main configuration file "smartmet.conf" of the SmartMet Server when using Docker.

## Prereqs

Docker software has been installed on some Linux server where you have access to already and the smartmetserver docker container is up and running.

### File smartmet.conf

The purpose of the main configuration file "smartmet.conf " is to define which plugins and engines are to be loaded when the server starts. It also defines the names and paths of the configuration files these plugins and engines are using. If you followed the “SmartMet Server Tutorial (Docker)” you have your configuration folders and files in the host machine at:

$HOME/docker-smartmetserver/smartmetconf

But inside Docker they show up under /etc/smartmet.

1.	Go to the correct directory and enter command below to review the file:
```
$ less smartmet.conf
```
You will see something like this:
```
// Options

accesslogdir = "/var/log/smartmet/";

port            = 80;

slowpool:
{
  maxthreads = 15;
  maxrequeuesize = 1000;
};

fastpool:
{
  maxthreads = 15;
  maxrequeuesize = 1000;
};


lazylinking = true;

defaultlogging = true;

debug           = true;
verbose         = true;

engines:
{
        sputnik:
        {
                configfile      = "/etc/smartmet/engines/sputnik.conf";
        };
```
2.	Find attribute **engines** to verify what engines are included. List of engines will look something like this:

```
engines:
{
        sputnik:
        {
                configfile      = "/etc/smartmet/engines/sputnik.conf";
        };
        contour:
        {
                configfile      = "/etc/smartmet/engines/contour.conf";
        };
        geonames:
        {
                disabled        = false;
                configfile      = "/etc/smartmet/engines/geonames.conf";
        };
        gis:
        {
                disabled        = false;
                configfile      = "/etc/smartmet/engines/gis.conf";
        };
        querydata:
        {
                configfile      = "/etc/smartmet/engines/querydata.conf";
        };
};
```
3.	Find attribute **plugins** to verify what plugins are included. List of plugins will look something like this:

```
plugins:
{
        admin:
        {
                configfile      = "/etc/smartmet/plugins/admin.conf";
        };
        download:
        {
                configfile      = "/etc/smartmet/plugins/download.conf";
        };
        timeseries:
        {
                configfile      = "/etc/smartmet/plugins/timeseries.conf";
        };
        wms:
        {
                configfile      = "/etc/smartmet/plugins/wms.conf";
        };
};
```

4. Use Nano or some other editor to enable/disable or add/remove engines and plugins if needed and notice that the "geoengine" entry must precede the "obsengine" entry as the ObsEngine uses the GeoEngine's functions.

Plugin and engine specific configuration tutorials can be found under the Plugin/Engine wiki page in question.