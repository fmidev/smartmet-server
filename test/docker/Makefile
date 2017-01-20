NAME = smartmet-server-test-db
TAG = docker.weatherproof.fi/$(NAME)
VERSION = 9.5-centos7

all: image tag push

image:
	docker build --tag="$(TAG):$(VERSION)" --rm .

tag:
	docker tag $(TAG):$(VERSION) $(TAG):latest

push:
	docker push $(TAG):latest

clean:
	-docker stop $(NAME)
	-docker rm $(NAME)
	-docker rmi $(TAG):$(VERSION)
