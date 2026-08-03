
SMARTMETD=$(shell test -e ../smartmetd && echo `pwd`/../smartmetd || echo /usr/sbin/smartmetd)

test: $(SMARTMETD) startup-test.sh conformance-test.sh conformance-test.py
	bash ./startup-test.sh $(SMARTMETD)
	@echo "Startup test OK"
	bash ./conformance-test.sh $(SMARTMETD)

clean:
	rm -f test.out test.status conformance.out conformance.conf
