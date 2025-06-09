# Simple QEMU PCIe Device

This repository contains the source code and scripts mentioned in [QEMU PCIe Device (Part 1)](andrelei.com/technical/qemu/qemu-pcie-test-device-part-1).

## Tested Setup

The code was tested using the following environment:

| Software              | Version |
| --------------------- | ------- |
| CMake                 | 3.31.7  |
| GCC                   | 12.3    |
| Debian (Guest OS)     | 12      |
| QEMU (x86_64 Machine) | 10.0    |

## Build QEMU

To prepare and build QEMU with the integrated PCIe device, run the following script:

```sh
./qemu-setup.sh
```

This script does the following:
1. Clone QEMU v10.0.
1. Create symlinks to the PCIe source code.
1. Patch QEMU to include the new files into the build.
1. Build `x86_64-softmmu` QEMU target

You will need to provide your own Linux kernel, and disk image to boot the system.

Update `qemu-start.sh` with your own `INIT_RD`, `KERNEL`, `QCOW2`.
Run `./qemu-start.sh` to start QEMU.

## Build Userspace Test Application

The test application, which interacts with the custom PCIe device from within the guest OS, is built using CMake.

To build the sanity-check application, execute these commands from the root of this repository:
```sh
$ cmake -B build --fresh
$ cmake --build build
```
The compiled binary will be located at build/src/userspace/sanity-check.

Transfer the binary to the guest OS and run `./sanity-check <BDF>`.
```sh
# ./sanity-check 0000:00:04.0
Version check passed ✓!
Testing read modify write
Scratch register write passed ✓!
```
