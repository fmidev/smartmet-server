## Docker installation on Ubuntu 16.04 LTS

`sudo apt-get install curl`

### Add repository key to package manager

Add Docker-Ubuntu's repository key to APT's trusted package sources:

`curl --fail --silent --show-error --location https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -`

### Install manager for software repositories

`sudo apt-get install software-properties-common`

`sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"`

`sudo apt-get update`

### Check availability of the docker-ce package
It should be available from previously installed Docker-repository:

`apt-cache policy docker-ce`

Should output something similar to:
```
docker-ce:
   Installed: (none)
...
```

### Install the docker-ce package

`sudo apt-get install -y docker-ce`

### Check Docker is running

`sudo systemctl status docker`

### Add your user to the docker group

`sudo usermod -aG docker ${USER}`

Take the new group in use with: `su - ${USER}`

## Docker compose installation

`sudo curl -L https://github.com/docker/compose/releases/download/1.18.0/docker-compose-$(uname -s)-$(uname -m) -o /usr/local/bin/docker-compose`

`sudo chmod +x /usr/local/bin/docker-compose`

### Check Docker compose is installed

`docker-compose --version`


## Docker installation on Ubuntu 18.04.1 LTS

`sudo apt install curl` (if missing)

### Add repository key to package manager

Add Docker-Ubuntu's repository key to APT's trusted package sources:

`curl --fail --silent --show-error --location https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -`

### Install manager for software repositories

`sudo apt install software-properties-common` (if missing)

`sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"`

`sudo apt update`

### Check availability of the docker-ce package
It should be available from previously installed Docker-repository:

`apt-cache policy docker-ce`

Should output something similar to:
```
docker-ce:
   Installed: (none)
...
```

### Install the docker-ce package

`sudo apt install -y docker-ce`

### Check Docker is running

`sudo systemctl status docker`

### Add your user to the docker group

`sudo usermod -aG docker ${USER}`

Take the new group in use with: `su - ${USER}`

## Docker compose installation

```
curl -L https://github.com/docker/compose/releases/download/1.18.0/docker-compose-$(uname -s)-$(uname -m) -o ~/docker-compose

sudo mv ~/docker-compose /usr/local/bin/docker-compose 
```

`sudo chmod +x /usr/local/bin/docker-compose`

### Check Docker compose is installed

`docker-compose --version`
Should ouput something similar to:
```
docker-compose version 1.18.0, build 8dd22a9
```