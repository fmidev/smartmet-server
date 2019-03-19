NAME = smartmet-server-test-db
TAG = docker.weatherproof.fi/$(NAME)
VERSION = 9.5-centos7

ifneq ($(HTTP_PROXY),)
	BUILD-ARG-HTTP_PROXY=--build-arg=http_proxy=$(HTTP_PROXY)
else ifneq ($(http_proxy),)
	BUILD-ARG-HTTP_PROXY=--build-arg=http_proxy=$(http_proxy)
endif
ifneq ($(HTTPS_PROXY),)
	BUILD-ARG-HTTPS_PROXY=--build-arg=https_proxy=$(HTTPS_PROXY)
else ifneq ($(https_proxy),)
	BUILD-ARG-HTTPS_PROXY=--build-arg=https_proxy=$(https_proxy)
endif
ifneq ($(NO_PROXY),)
	BUILD-ARG-NO_PROXY=--build-arg=no_proxy=$(NO_PROXY)
else ifneq ($(no_proxy),)
	BUILD-ARG-NO_PROXY=--build-arg=no_proxy=$(no_proxy)
endif


all: image tag push

image:
	docker build $(BUILD-ARG-HTTP_PROXY) $(BUILD-ARG-HTTPS_PROXY) $(BUILD-ARG-NO_PROXY) --tag="$(TAG):$(VERSION)" --rm .

tag:
	docker tag $(TAG):$(VERSION) $(TAG):latest

push:
	docker push $(TAG):latest

clean:
	-docker stop $(NAME)
	-docker rm $(NAME)
	-docker rmi $(TAG):$(VERSION)
