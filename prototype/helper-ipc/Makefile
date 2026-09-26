CC ?= clang
CFLAGS = -fobjc-arc -Wall -Wextra -Werror -O2
JDK_HOME := $(shell /usr/libexec/java_home -v 25)

.PHONY: build clean smoke package-smoke daemon-prepare baseline-read-test baseline-contract-test baseline-watch-test watch-contract-test

build: helper-status

helper-status: helper-status.m HelperStatus.h HelperBaselineValidation.h
	$(CC) $(CFLAGS) -framework Foundation helper-status.m -o $@

smoke: build
	./smoke.sh

agent-registration: agent-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement agent-registration.m -o $@

daemon-status: daemon-status.m HelperStatus.h SmcBaselineRead.c SmcBaselineRead.h BaselineWatch.c BaselineWatch.h BaselineWatchController.m BaselineWatchController.h
	$(CC) $(CFLAGS) -framework Foundation -framework IOKit \
		daemon-status.m BaselineWatchController.m SmcBaselineRead.c BaselineWatch.c -o $@

baseline-read-test: SmcBaselineReadTest
	./SmcBaselineReadTest

baseline-watch-test: BaselineWatchTest
	./BaselineWatchTest

BaselineWatchTest: BaselineWatchTest.c BaselineWatch.c BaselineWatch.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		BaselineWatchTest.c BaselineWatch.c SmcBaselineRead.c -o $@

baseline-contract-test: HelperBaselineValidationTest
	./HelperBaselineValidationTest

watch-contract-test: HelperWatchValidationTest
	./HelperWatchValidationTest

HelperWatchValidationTest: HelperWatchValidationTest.m HelperWatchValidation.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperWatchValidationTest.m -o $@

HelperBaselineValidationTest: HelperBaselineValidationTest.m HelperBaselineValidation.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperBaselineValidationTest.m -o $@

SmcBaselineReadTest: SmcBaselineReadTest.c SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		SmcBaselineReadTest.c SmcBaselineRead.c -o $@

libhelper-probe.dylib: HelperProbeBridge.m HelperStatus.h HelperBaselineValidation.h HelperWatchValidation.h
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
	rm -f helper-status agent-registration daemon-status daemon-registration libhelper-probe.dylib SmcBaselineReadTest HelperBaselineValidationTest BaselineWatchTest HelperWatchValidationTest
