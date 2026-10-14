-include .flowstate-local.mk

.DEFAULT_GOAL := all
BUILD_TYPE ?= RelWithDebInfo

ifneq ($(QT_PREFIX),)
export CMAKE_PREFIX_PATH := $(QT_PREFIX):$(CMAKE_PREFIX_PATH)
export LD_LIBRARY_PATH := $(QT_PREFIX)/lib:$(LD_LIBRARY_PATH)
export QT_PLUGIN_PATH := $(QT_PREFIX)/lib/qt6/plugins:$(QT_PLUGIN_PATH)
export QML_IMPORT_PATH := $(QT_PREFIX)/lib/qt6/qml:$(QML_IMPORT_PATH)
endif

all:
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build build -j

run: all
	./scripts/run-nested.sh --qml

test: all
	ctest --test-dir build --output-on-failure -LE stress

test-all: all
	ctest --test-dir build --output-on-failure

debug:
	$(MAKE) BUILD_TYPE=Debug all

clean:
	cmake -E rm -rf build

nested: all
	./scripts/run-nested.sh

help:
	@printf '%s\n' 'make           Configure and incrementally build (RelWithDebInfo)' \
	  'make run       Open the nested Vulkan compositor, QML panel and terminals' \
	  'make nested    Open the nested compositor with three terminals' \
	  'make test      Run unit, integration and graphical tests' \
	  'make test-all  Include repeated-start stress tests' \
	  'make debug     Build with debugging symbols and no optimization' \
	  'make clean     Remove the generated build directory' \
	  'make help      Show these commands'

.PHONY: all run test test-all debug clean nested help
