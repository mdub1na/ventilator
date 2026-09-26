CC ?= clang
CFLAGS = -fobjc-arc -Wall -Wextra -Werror -O2

.PHONY: build clean smoke package-smoke daemon-prepare

build: helper-status

helper-status: helper-status.m HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation helper-status.m -o $@

smoke: build
	./smoke.sh

agent-registration: agent-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement agent-registration.m -o $@

daemon-status: daemon-status.m HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation daemon-status.m -o $@

daemon-registration: daemon-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement daemon-registration.m -o $@

daemon-prepare: build daemon-status daemon-registration
	./daemon-probe.sh prepare

package-smoke: build agent-registration
	./package-smoke.sh

clean:
	rm -f helper-status agent-registration daemon-status daemon-registration
