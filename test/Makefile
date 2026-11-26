
SMARTMETD=$(shell test -e ../smartmetd && echo `pwd`/../smartmetd || echo /usr/sbin/smartmetd)

test: $(SMARTMETD) startup-test.sh conformance-test.sh conformance-test.py cancellation-test.sh
	bash ./startup-test.sh $(SMARTMETD)
	@echo "Startup test OK"
	bash ./conformance-test.sh $(SMARTMETD)
	$(MAKE) -C cancel_plugin
	bash ./cancellation-test.sh $(SMARTMETD)

clean:
	rm -rf test.out test.status conformance.out conformance.conf conformance-logs
	rm -rf cancellation.out cancellation.conf cancellation-logs
	$(MAKE) -C cancel_plugin clean
