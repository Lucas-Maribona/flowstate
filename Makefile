-include .flowstate-local.mk

ifneq ($(QT_PREFIX),)
export CMAKE_PREFIX_PATH := $(QT_PREFIX):$(CMAKE_PREFIX_PATH)
export LD_LIBRARY_PATH := $(QT_PREFIX)/lib:$(LD_LIBRARY_PATH)
export QT_PLUGIN_PATH := $(QT_PREFIX)/lib/qt6/plugins:$(QT_PLUGIN_PATH)
export QML_IMPORT_PATH := $(QT_PREFIX)/lib/qt6/qml:$(QML_IMPORT_PATH)
endif

all:
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build build -j

test: all
	ctest --test-dir build --output-on-failure

nested: all
	./scripts/run-nested.sh

.PHONY: all test nested
