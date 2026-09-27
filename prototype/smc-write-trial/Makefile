CC := xcrun clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

.PHONY: build test clean

build: smc-write-trial

smc-write-trial: smc-write-trial.c trial_logic.c trial_logic.h trial_actions.c trial_actions.h
	$(CC) $(CFLAGS) smc-write-trial.c trial_logic.c trial_actions.c -framework IOKit -framework CoreFoundation -o $@

trial-logic-test: trial_logic_test.c trial_logic.c trial_logic.h
	$(CC) $(CFLAGS) trial_logic_test.c trial_logic.c -o $@

trial-actions-test: trial_actions_test.c trial_actions.c trial_actions.h trial_logic.c trial_logic.h
	$(CC) $(CFLAGS) trial_actions_test.c trial_actions.c trial_logic.c -o $@

test: smc-write-trial trial-logic-test trial-actions-test
	./trial-logic-test
	./trial-actions-test
	bash ./ftst-supervised-test.sh
	@for command in 'trial TRIAL-Mac15,7-27.0' 'ftst-check FTST-CHECK-Mac15,7-27.0'; do \
		set -- $$command; \
		output=$$(./smc-write-trial "$$1" --apply --confirm "$$2" 2>&1); status=$$?; \
		test $$status -ne 0 && test "$$output" = 'hardware trials suspended after delayed Ftst state change; no SMC access attempted' || exit 1; \
	done
	@output=$$(./smc-write-trial ftst-check --apply-reviewed --confirm FTST-REVIEWED-Mac15,7-27.0 2>&1); status=$$?; \
		test $$status -eq 2 && test "$$output" = 'reviewed Ftst trial completed; further hardware trials suspended before SMC access'

clean:
	rm -f smc-write-trial trial-logic-test trial-actions-test
