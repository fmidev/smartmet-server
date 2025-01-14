
SMARTMETD=$(shell test -e ../smartmetd && echo `pwd`/../smartmetd || echo /usr/sbin/smartmetd)

test: $(SMARTMETD) startup-test.sh
	bash ./startup-test.sh $(SMARTMETD)
	@echo "Startup test OK"

clean:
	rm -f test.out test.status
	rm -rf log
