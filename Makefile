# Convenience targets for Backdrop.
#   make              # configure + build
#   make install      # install to PREFIX (default /usr/local; often needs sudo)
#   make uninstall
#   make dist         # source tarball in dist/
#   make package-rpm
#   make package-flatpak

PREFIX ?= /usr/local
BUILD_DIR ?= build
BUILD_TYPE ?= Release

.PHONY: all build configure install uninstall dist package-rpm package-flatpak clean

all: build

configure:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DCMAKE_INSTALL_PREFIX="$(PREFIX)"

build: configure
	cmake --build "$(BUILD_DIR)" -j"$$(nproc 2>/dev/null || echo 4)"

install: build
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

uninstall:
	./scripts/uninstall.sh

dist:
	./scripts/dist.sh

package-rpm:
	./scripts/package-rpm.sh

package-flatpak:
	./scripts/package-flatpak.sh

clean:
	rm -rf "$(BUILD_DIR)" dist
