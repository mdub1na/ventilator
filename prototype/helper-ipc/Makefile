CC ?= clang
CFLAGS = -fobjc-arc -Wall -Wextra -Werror -O2

.PHONY: build clean smoke

build: helper-status

helper-status: helper-status.m HelperStatus.h
	$(CC) $(CFLAGS) -framework Foundation helper-status.m -o $@

smoke: build
	./smoke.sh

clean:
	rm -f helper-status
