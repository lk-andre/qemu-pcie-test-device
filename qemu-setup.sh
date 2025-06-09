#!/bin/bash

set -e

SUBMODULE_PATH="external/qemu"
QEMU_TARGET="x86_64-softmmu"
QEMU_TESTDEVICE_SOURCE="src/qemu/pcie-testdevice.c"
QEMU_TESTDEVICE_HEADER="include/pcie_device_regs.h"
QEMU_PATCH="qemu-build-sys.patch"

echo "Initialize submodule"
git submodule update --init --depth 1 "$SUBMODULE_PATH"

echo "Symlink test device into QEMU directory"
ln -sf "../../../../$QEMU_TESTDEVICE_SOURCE" "$SUBMODULE_PATH/hw/misc"
ln -sf "../../../../../$QEMU_TESTDEVICE_HEADER" "$SUBMODULE_PATH/include/hw/misc"

echo "Apply build system patch"
patch -d "$SUBMODULE_PATH" -p1 < "$QEMU_PATCH"

echo "Configure QEMU"
pushd .
mkdir -p "$SUBMODULE_PATH/build"
cd "$SUBMODULE_PATH/build"
../configure --target-list="$QEMU_TARGET" --enable-debug

echo "Building QEMU"
make

popd
echo "QEMU build completed"
