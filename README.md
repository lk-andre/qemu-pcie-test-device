# Simple QEMU PCIe Device

This repository contains the source code and scripts mentioned in [QEMU PCIe Device (Part 1)](andrelei.com/technical/qemu/qemu-pcie-test-device-part-1) and [QEMU PCIe Device (Part 2)](andrelei.com/technical/qemu/qemu-pcie-test-device-part-2).

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

1. Clones QEMU v10.0.
1. Creates symlinks to the PCIe source code.
1. Patches QEMU to include the new files into the build.
1. Builds the `x86_64-softmmu` QEMU target

You will need to provide your own Linux kernel, and disk image to boot the system.

Update `qemu-launch.sh` with your own `INIT_RD`, `KERNEL`, `QCOW2`.
Run `./qemu-launch.sh` to start QEMU.

## Build the Kernel Module and Userspace Applications

The kernel module and the test application, which interacts with the custom PCIe device from within the guest OS, is built using CMake.

Note that for the kernel module, you will need to provide the correct Linux headers.
The `KERNEL_HEADER_DIR` argument is passed into CMake to specify the location of the headers.

To build, execute these commands from the root of this repository:

```sh
$ cmake -D KERNEL_HEADER_DIR=<kernal header path> -B build --fresh
$ cmake --build build
```

You should see an output similar to this:

```sh
$ cmake -DKERNEL_HEADER_DIR=/usr/src/linux-headers-6.1.0-34-amd64 -B build --fresh
-- The C compiler identification is GNU 12.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.3s)
-- Generating done (0.0s)
-- Build files have been written to: /home/andrelei/Documents/github/qemu-pcie-test-device/build
$ cmake --build build
[ 20%] Running Kernel Build
[ 20%] Built target pcie-test-module
[ 40%] Building C object src/userspace/CMakeFiles/sanity-check.dir/sanity-check.c.o
[ 60%] Linking C executable sanity-check
[ 60%] Built target sanity-check
[ 80%] Building C object src/userspace/CMakeFiles/dma-check.dir/dma-kernel-module-test.c.o
[100%] Linking C executable dma-check
[100%] Built target dma-check
```

The compiled kernel module will be located at `build/src/kernel/`.
The test applications will be located at `build/src/userspace/sanity-check` and `build/src/userspace/dma-check`.

### Running `sanity-check`

Transfer the binary to the guest OS and run `./sanity-check <BDF>`.

```sh
# ./sanity-check 0000:00:04.0
Version check passed ✓!
Testing read modify write
Scratch register write passed ✓!
```

### Running `dma-check`

Transfer both `pcie-test-module.ko` and `dma-check` to the guest OS.

Load the kernel module `pcie-test-module.ko`.

```sh
root@debian:/home/andre# insmod pcie-test-module.ko
[   57.312310] pcie_test_module: loading out-of-tree module taints kernel.
[   57.326840] pcie_test_module: module verification failed: signature and/or required key missing - tainting kernel
[   57.354805] ACPI: \_SB_.GSIE: Enabled at IRQ 20
```

Run the DMA test:

```sh
root@debian:/home/andre# ./dma-check pcietest0
Running Kernel module tests
--- Checking Version Register ---
--- Testing Force Trigger Interrupt ---
Checking Interrupt Status First- Force Interrupt
--- Testing Device Memory ---
Writing 32 bytes (decrementing pattern) to DMA buffer @ 0x0 from userspace...
Writing 32 bytes (incrementing pattern) to DMA buffer @ 0x120 from userspace...
Transfer contents from DMA buffer to device (32 bytes @ 0x0 to 0x0)
Transfer contents from DMA buffer to device (32 bytes @ 0x120 to 0x120)
Transfer contents from device to DMA buffer (32 bytes @ 0x0 to 0x120)
Transfer contents from device to DMA buffer (32 bytes @ 0x120 to 0x0)
Transfer contents device to DMA buffer (16 bytes @ 0x0 to 0xfff0)
Checking buffer content
Kernel module tests passed ✓!
```
