CC := xcrun clang
JAVA_HOME := $(shell /usr/libexec/java_home -v 25)

.PHONY: build clean

build: liblogin-item.dylib

liblogin-item.dylib: LoginItemBridge.m
	$(CC) -dynamiclib -fobjc-arc -fblocks -Wall -Wextra -Werror -mmacosx-version-min=13.0 \
		-I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/darwin \
		-framework AppKit -framework ServiceManagement -framework Carbon $< -o $@

clean:
	rm -f liblogin-item.dylib
