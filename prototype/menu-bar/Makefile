CC := xcrun swiftc
MODULE_CACHE := $(CURDIR)/build/module-cache
CLANG_CACHE := $(CURDIR)/build/clang-cache

.PHONY: build run clean

build: status-item status-item-bridge

status-item: StatusItem.swift StatusArtwork.swift
	mkdir -p $(MODULE_CACHE) $(CLANG_CACHE)
	CLANG_MODULE_CACHE_PATH=$(CLANG_CACHE) $(CC) -module-cache-path $(MODULE_CACHE) $^ -o $@

status-item-bridge: StatusItemBridge.swift StatusArtwork.swift
	mkdir -p $(MODULE_CACHE) $(CLANG_CACHE)
	CLANG_MODULE_CACHE_PATH=$(CLANG_CACHE) $(CC) -module-cache-path $(MODULE_CACHE) $^ -o $@

run: status-item
	./status-item ../smc-read/smc-read

clean:
	rm -f status-item status-item-bridge
