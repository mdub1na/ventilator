CC := xcrun clang
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

.PHONY: build run test clean

build: smc-read

smc-read: smc-read.c
	$(CC) $(CFLAGS) $< -framework IOKit -framework CoreFoundation -o $@

run: smc-read
	./smc-read

test:
	python3 sensor-availability-test.py

clean:
	rm -f smc-read
