Table of Contents
=================
  * [Table of Contents](#table-of-contents)
  * [Querydata engine does not work](#querydata-engine-does-not-work)
  * [Customizing proxy settings](#customizing-proxy-settings)
  * [TimeSeries plugin does not work](#timeseries-plugin-does-not-work)

# Querydata engine does not work

When trying to test the container, the query below does not return anything and gives no error message either:
```
http://hostname:8080/admin?what=qengine
```
Note: Replace hostname with your host machine name, by localhost or by host-ip. This depends on where you have the container you are using.

1. Review file querydata.conf. If you followed the “SmartMet Server Tutorial (Docker)” you have your configuration folders and files in the host machine at $HOME/docker-smartmetserver/smartmetconf but inside Docker they show up under /etc/smartmet. Go to the correct directory and enter command below for example:

```
$ less querydata.conf
```
You will see something like this:
```
verbose = true;

# Note: order is significant
producers =
[
        "hirlam_europe_surface"
#        "hirlam-knmi_europe_surface",
#       "icon_world_surface",
#       "icon_world_pressure",
#       "gfs_world_surface",
#       "gfs_world_pressure",
#       "gem_world_surface",
#       "gem_world_pressure",
#       "gensavg_world_surface",
#       "gensctrl_world_surface",
#       "hbm-fmi"
];

// types: grid, points
// leveltypes: surface, pressure, model

hirlam_europe_surface:
{
        alias                   = "hirlam";
        directory               = "/smartmet/data/hirlam/eurooppa/pinta/querydata";
        pattern                 = ".*_hirlam_eurooppa_pinta\.sqd$";
        forecast                = true;
        type                    = "grid";
        leveltype               = "surface";
        refresh_interval_secs   = 60;
        number_to_keep          = 4;
        multifile               = true;
};

hirlam_europe_pressure:
{
        alias                   = "hirlam_pressure";
        directory               = "/smartmet/data/hirlam/pressure";
        pattern                 = ".*_hirlam_europe_pressure\.sqd$";
        forecast                = true;
        type                    = "grid";
        leveltype               = "pressure";
        refresh_interval_secs   = 60;
        number_to_keep          = 2;
};

etc...
```

2. Go inside the container. 
```
$ docker exec -i -t contaner_id /bin/bash
```
3. Check if the directory paths of each producer that you are really using are the same in the container as they are in the configuration file. If they are not the same modify the querydata.conf on your host machine to match the container directory structure.

4. Retest the issue

# Customizing proxy settings

1. Review steps defined at [**Docker docs**](https://docs.docker.com/engine/admin/systemd/) under "Control and configure Docker with systemd / HTTP/HTTPS proxy":
```
https://docs.docker.com/engine/admin/systemd/
```
# TimeSeries plugin does not work

When trying to test the plugin, the query returns error messages only.

1. Run for example the following request to fetch the temperature forecasted for the city of Helsinki:

```
http://hostname:8080/timeseries?format=debug&place=Helsinki&param=name,time,temperature
```

**Note:** Replace hostname with your host machine name, by localhost or by host-ip. This depends on where you have the container you are using.

It returns errors like below:

![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/TimeSeriesErrors.PNG)

2. Make sure that the FmiNames database is available in your docker installation and file geonames.conf has been configured correctly. You should have a separate docker-image fmidev/fminames-noreplicate.

3. Add the missing setups and retest the issue