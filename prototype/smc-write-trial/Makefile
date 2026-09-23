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

test: trial-logic-test trial-actions-test
	./trial-logic-test
	./trial-actions-test

clean:
	rm -f smc-write-trial trial-logic-test trial-actions-test
