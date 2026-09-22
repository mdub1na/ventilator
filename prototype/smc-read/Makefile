CC := xcrun clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

.PHONY: build run clean

build: smc-read

smc-read: smc-read.c
	$(CC) $(CFLAGS) $< -framework IOKit -framework CoreFoundation -o $@

run: smc-read
	./smc-read

clean:
	rm -f smc-read
