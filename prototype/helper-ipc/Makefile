CC ?= clang
CFLAGS = -fobjc-arc -Wall -Wextra -Werror -O2
JDK_HOME = $(shell /usr/libexec/java_home -v 25)

.PHONY: build clean smoke package-smoke daemon-prepare baseline-read-test baseline-contract-test baseline-watch-test watch-contract-test startup-audit-test control-lease-test control-intent-test worker-supervisor-test supervisor-probe-test

build: helper-status

helper-status: helper-status.m HelperStatus.h HelperBaselineValidation.h HelperTemperatureValue.h SmcBaselineRead.h
	$(CC) $(CFLAGS) -framework Foundation helper-status.m -o $@

smoke: build
	./smoke.sh

agent-registration: agent-registration.m
	$(CC) $(CFLAGS) -framework Foundation -framework ServiceManagement agent-registration.m -o $@

daemon-status: daemon-status.m HelperStatus.h HelperTemperatureValue.h SmcBaselineRead.c SmcBaselineRead.h BaselineWatch.c BaselineWatch.h BaselineWatchController.m BaselineWatchController.h StartupAuditController.m StartupAuditController.h SupervisorProbeController.m SupervisorProbeController.h HelperSupervisorValidation.h
	$(CC) $(CFLAGS) -framework Foundation -framework IOKit -framework Security \
		daemon-status.m BaselineWatchController.m StartupAuditController.m SupervisorProbeController.m SmcBaselineRead.c BaselineWatch.c -o $@

supervisor-probe: supervisor-probe.c SupervisorProbeStorage.c SupervisorProbeStorage.h WorkerSupervisor.c WorkerSupervisor.h SupervisorOwnership.c SupervisorOwnership.h ControlIntentJournal.c ControlIntentJournal.h ControlLease.c ControlLease.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -pthread -framework IOKit \
		supervisor-probe.c SupervisorProbeStorage.c WorkerSupervisor.c SupervisorOwnership.c ControlIntentJournal.c ControlLease.c SmcBaselineRead.c -o $@

supervisor-probe-test: HelperSupervisorValidationTest SupervisorProbeStorageTest supervisor-probe daemon-status
	./HelperSupervisorValidationTest
	./SupervisorProbeStorageTest
	@test "$$(id -u)" != 0 || { echo "run the refusal test without sudo" >&2; exit 1; }; \
	status=0; ./supervisor-probe >/dev/null 2>&1 || status=$$?; test "$$status" = 2; \
	status=0; ./supervisor-probe --cleanup >/dev/null 2>&1 || status=$$?; test "$$status" = 2

HelperSupervisorValidationTest: HelperSupervisorValidationTest.m HelperSupervisorValidation.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperSupervisorValidationTest.m -o $@

SupervisorProbeStorageTest: SupervisorProbeStorageTest.c SupervisorProbeStorage.c SupervisorProbeStorage.h SupervisorOwnership.c SupervisorOwnership.h ControlIntentJournal.c ControlIntentJournal.h ControlLease.c ControlLease.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -pthread -framework IOKit \
		SupervisorProbeStorageTest.c SupervisorProbeStorage.c SupervisorOwnership.c ControlIntentJournal.c ControlLease.c SmcBaselineRead.c -o $@

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

startup-audit-test: StartupAuditControllerTest
	./StartupAuditControllerTest

control-lease-test: ControlLeaseTest
	./ControlLeaseTest

ControlLeaseTest: ControlLeaseTest.c ControlLease.c ControlLease.h ControlIntentJournal.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		ControlLeaseTest.c ControlLease.c SmcBaselineRead.c -o $@

control-intent-test: ControlIntentJournalTest
	./ControlIntentJournalTest

worker-supervisor-test: WorkerSupervisorTest
	./WorkerSupervisorTest

WorkerSupervisorTest: WorkerSupervisorTest.c WorkerSupervisor.c WorkerSupervisor.h SupervisorOwnership.c SupervisorOwnership.h ControlIntentJournal.c ControlIntentJournal.h ControlLease.c ControlLease.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -pthread -framework IOKit \
		WorkerSupervisorTest.c WorkerSupervisor.c SupervisorOwnership.c ControlIntentJournal.c ControlLease.c SmcBaselineRead.c -o $@

ControlIntentJournalTest: ControlIntentJournalTest.c ControlIntentJournal.c ControlIntentJournal.h ControlLease.c ControlLease.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		ControlIntentJournalTest.c ControlIntentJournal.c ControlLease.c SmcBaselineRead.c -o $@

StartupAuditControllerTest: StartupAuditControllerTest.m StartupAuditController.m StartupAuditController.h HelperStartupAuditValidation.h HelperBaselineValidation.h HelperTemperatureValue.h HelperStatus.h SmcBaselineRead.c SmcBaselineRead.h
	$(CC) $(CFLAGS) -framework Foundation -framework IOKit \
		StartupAuditControllerTest.m StartupAuditController.m SmcBaselineRead.c -o $@

HelperWatchValidationTest: HelperWatchValidationTest.m HelperWatchValidation.h HelperTemperatureValue.h SmcBaselineRead.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperWatchValidationTest.m -o $@

HelperBaselineValidationTest: HelperBaselineValidationTest.m HelperBaselineValidation.h HelperTemperatureValue.h SmcBaselineRead.h HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation HelperBaselineValidationTest.m -o $@

SmcBaselineReadTest: SmcBaselineReadTest.c SmcBaselineRead.c SmcBaselineRead.h
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -framework IOKit \
		SmcBaselineReadTest.c SmcBaselineRead.c -o $@

libhelper-probe.dylib: HelperProbeBridge.m HelperStatus.h HelperBaselineValidation.h HelperWatchValidation.h HelperStartupAuditValidation.h HelperSupervisorValidation.h HelperTemperatureValue.h SmcBaselineRead.h
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
	rm -f helper-status agent-registration daemon-status daemon-registration libhelper-probe.dylib SmcBaselineReadTest HelperBaselineValidationTest BaselineWatchTest HelperWatchValidationTest StartupAuditControllerTest ControlLeaseTest ControlIntentJournalTest WorkerSupervisorTest supervisor-probe HelperSupervisorValidationTest SupervisorProbeStorageTest
