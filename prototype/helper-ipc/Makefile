CC ?= clang
CFLAGS = -fobjc-arc -Wall -Wextra -Werror -O2
JDK_HOME := $(shell /usr/libexec/java_home -v 25)

.PHONY: build clean smoke package-smoke daemon-prepare baseline-read-test baseline-contract-test

build: helper-status

helper-status: helper-status.m HelperStatus.h HelperBaselineValidation.h
	$(CC) $(CFLAGS) -framework Foundation helper-status.m -o $@

smoke: build
	./smoke.sh

agent-registration: agent-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement agent-registration.m -o $@

daemon-status: daemon-status.m HelperStatus.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) $(CFLAGS) -framework Foundation -framework IOKit \
		daemon-status.m SmcBaselineRead.c -o $@

baseline-read-test: SmcBaselineReadTest
	./SmcBaselineReadTest

baseline-contract-test: HelperBaselineValidationTest
	./HelperBaselineValidationTest

HelperBaselineValidationTest: HelperBaselineValidationTest.m HelperBaselineValidation.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperBaselineValidationTest.m -o $@

SmcBaselineReadTest: SmcBaselineReadTest.c SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		SmcBaselineReadTest.c SmcBaselineRead.c -o $@

libhelper-probe.dylib: HelperProbeBridge.m HelperStatus.h HelperBaselineValidation.h
	$(CC) $(CFLAGS) -dynamiclib -fblocks -mmacosx-version-min=13.0 \
		-I$(JDK_HOME)/include -I$(JDK_HOME)/include/darwin \
		-framework Foundation -framework Security -framework ServiceManagement \
		HelperProbeBridge.m -o $@

daemon-registration: daemon-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement daemon-registration.m -o $@

daemon-prepare: build daemon-status daemon-registration
	./daemon-probe.sh prepare

package-smoke: build agent-registration
	./package-smoke.sh

clean:
	rm -f helper-status agent-registration daemon-status daemon-registration libhelper-probe.dylib SmcBaselineReadTest HelperBaselineValidationTest
