# Convenience wrapper around the CMake build; CMakeLists.txt remains the
# single source of truth for project sources and dependencies.
CMAKE ?= $(shell command -v cmake3 2>/dev/null || command -v cmake)
BUILD_DIR ?= build

.PHONY: all configure build clean install

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

clean:
	@if test -f $(BUILD_DIR)/Makefile; then \
		$(CMAKE) --build $(BUILD_DIR) --target clean; \
	fi

install: build
	$(CMAKE) --install $(BUILD_DIR)
