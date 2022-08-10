# Tutorial

This tutorial explains how to setup the SmartMet Server using Docker.

## Prereqs

Docker software has been installed on some Linux server where you have access to already.

## Getting Docker version of the SmartMet Server for external use

1. Go to the place where the public Docker contents are to verify what is available: 
```
https://hub.docker.com/
```
2. Search for Smartmet and you will get a page like below. Select fmidev/smartmetserver.

![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/Smartmet-public.png)

3. Load configuration files to your host machine. 

That is useful in case you will need to remove the container and recreate it with new parameters for some reason later (existing container cannot be changed). That way you will not loose the configuration done already even if the container is removed.

  a. Go to the configuration repository in github to get the configuration files:

```
https://github.com/fmidev/docker-smartmetserver.git
```

  b. Click the **Clone or Download** button

  c. Select **Download ZIP** and save the file

**Note:** Another way to get the files is using:

```
$ git clone https://github.com/fmidev/docker-smartmetserver.git
```

Git clone will create default folder structures like below for the configuration files:
```
$HOME/docker-smartmetserver/smartmetconf
$HOME/docker-smartmetserver/smartmetconf/engines
$HOME/docker-smartmetserver/smartmetconf/plugins
```
4. Create the docker image

There are two ways to create the docker image. You can build a fresh image by using **docker build** command or you can get the existing image of fmidev/smartmetserver by using **docker pull** command.

**Using the docker build command**

Create a fresh image using the dockerfile that is located on your machine.
```
$ docker build --no-cache -t fmidev/smartmetserver .
```

**Using the docker pull command**

Create the image using the existing image of fmidev/smartmetserver.

```
$ docker pull fmidev/smartmetserver
```
![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/Docker-pull.png)

5. Verify the fmidev/smartmetserver image is there by command **docker images**
```
$ docker images
```
![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/Docker-images.png)

6. Start the SmartMet server locally by running the **docker run** command. Command below will do the following:

* Creates a writeable container layer (smartmetserver) over the specified image (fmidev/smartmetserver)
* Starts the smartmetserver container
* Makes the container to run in the background (-d)
* Gets it restarted always regardless of the exit status
* Uses folder $HOME/smartmet-data for data
* Uses folder $HOME/docker-smartmetserver/smartmetconf for configuration files
(files were retrieved in step 3)
* Binds port 80 of the container to port 8080 on the host machine.
```
$ docker run -d --restart=always --name smartmetserver -v $HOME/smartmet-data:/smartmet/data -v $HOME/docker-smartmetserver/smartmetconf:/etc/smartmet -p 8080:80 fmidev/smartmetserver
```
7. Test your container in browser with examples below. 

  a. Query below should return the broadcast cluster status information at the frontend.
``` 
http://hostname:8080/admin?what=clusterinfo
```
<pre><code>
<b>Broadcast Cluster Information </b>
This server is a FRONTEND 
* Host: FServer
* Comment: SmartMet server in FServer
* HTTP interface: Host IP:port
* Throttle Limit: 0
* Broadcast Interface: Broadcast IP address

<b> Services known by the frontend server </b>
* URI's of different services and plugins
</code></pre>

  b. QEngine maintains QueryData in memory. Query below can be used to obtain the information about the currently loaded QueryData:
```
http://hostname:8080/admin?what=qengine
```
![](https://github.com/fmidev/smartmet-plugin-wms/wiki/images/QengineData.PNG)

**Note:** Replace hostname with your host machine name, by localhost or by host-ip. This depends on where you have the container you are using.

**Additional steps:**

  a. You can get data from external sources using the script you can find from:
```
https://github.com/fmidev/opendata-resources/tree/master/examples/fmiopendata-client/python
```
  b. Change data to querydata using the instructions you can find from:
```
https://github.com/fmidev/smartmet-qdtools/wiki/gribtoqd
```
This is needed when data is imported from external sources to SmartMet-system.

**Configuration**

Review [**Main Configuration File Tutorial (Docker)**](https://github.com/fmidev/smartmet-server/wiki/Main-Configuration-File-Tutorial-(Docker)) for configuring the main configuration file smartmet.conf.
Plugin and engine specific configuration tutorials can be found under the Plugin wiki page in question.


## Building instructions for a local Docker version of SmartMet Server (FMI internal) 

## Prereqs

Docker and Docker Compose

https://github.com/fmidev/smartmet-server/wiki/Setting-up-Docker-and-Docker-Compose-(Ubuntu-16.04)

## Installation

### 1. Clone the smartmet-server-install repository from your home dir

```
cd ~
```

```
git clone https://github.com/fmidev/smartmet-server-install.git
```

```
cd smartmet-server-install
```

### 2. Build a fresh image

```
docker build --no-cache --tag smartmet-server .
```

#### Errors during build and solutions for them

If the following error or similar occurs:
```
Get https://registry-1.docker.io/v2/: dial tcp 34.233.151.211:443: connect: connection refused
```
Add proxy settings to Docker systemd settings. See [Docker documentation](https://docs.docker.com/config/daemon/systemd/#httphttps-proxy) or [StackOverflow solution](https://stackoverflow.com/questions/23111631/cannot-download-docker-images-behind-a-proxy#28093517).

If the following error occurs:
```
Got permission denied while trying to connect to the Docker daemon socket at...
```
Your username may not be a member of the `docker` group. Use `usermod -aG docker ${USER}`, then logout and login.

**For builds inside a VirtualBox** (Ubuntu 18.04.1); if during the build you get the following error:
```
curl: (5) Could not resolve proxy...
error: skipping https://dl.fedoraproject.org/pub/epel/...
...
```
It may be that the container should use the same network settings as the virtual host. Add `--network host` to the build command.

### 3. Run the image with Docker Compose

```
docker-compose up smartmet-server
```

4. Check the status of SmartMet Server

```
http://localhost:8080/admin?what=clusterinfo
```

