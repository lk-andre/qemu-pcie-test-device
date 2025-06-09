#include <stdio.h>

#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pcie_device_regs.h"

#define RESOURCE0_GENERIC_PCIE_PATH "/sys/bus/pci/devices/%s/resource0"

int main(int argc, char *argv[])
{
    int fd;
    char resourcePciePath[512];

    const uint32_t EXPECTED_VERSION = 0x0101;

    if (argc < 2) {
        fprintf(stderr, "ERROR: Missing PCI resource!\n");
        return 1;
    }
    snprintf(resourcePciePath, sizeof(resourcePciePath), RESOURCE0_GENERIC_PCIE_PATH, argv[1]);

    fd = open(resourcePciePath, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open %s!\n", resourcePciePath);
        return 2;
    }

    void *mmap_bar0 = mmap(0, PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mmap_bar0 == MAP_FAILED) {
        fprintf(stderr, "ERROR: mmap failed!\n");
        return 3;
    }

    volatile uint32_t version, value;
    volatile uint32_t *regPtr = (volatile uint32_t *)mmap_bar0;

    printf("Checking Version Register\n");
    version = *(regPtr + PCIE_TEST_DEVICE_MMIO_VER_OFFSET / sizeof(uint32_t));

    if (version != EXPECTED_VERSION) {
        fprintf(stderr, "ERROR: Mismatch version! 0x%x != 0x%x\n", version, EXPECTED_VERSION);
    } else {
        printf("Version check passed \xE2\x9C\x93!\n");
    }

    printf("Testing read modify write\n");
    *(regPtr + PCIE_TEST_DEVICE_MMIO_SCRATCH_OFFSET / sizeof(uint32_t)) = 0;
    value = *(regPtr + PCIE_TEST_DEVICE_MMIO_SCRATCH_OFFSET / sizeof(uint32_t));
    if (value != 0) {
        fprintf(stderr, "ERROR: Scratch register should be 0x0!\n");
    }
    value ^= 0x55555555;
    *(regPtr + PCIE_TEST_DEVICE_MMIO_SCRATCH_OFFSET / sizeof(uint32_t)) = value;

    value = *(regPtr + PCIE_TEST_DEVICE_MMIO_SCRATCH_OFFSET / sizeof(uint32_t));
    if (value != 0x55555555) {
        fprintf(stderr, "ERROR: Scratch register should be 0x55555555!\n");
    } else {
        printf("Scratch register write passed \xE2\x9C\x93!\n");
    }

    // Cleanup before before checking expected version
    munmap(mmap_bar0, PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES);
    close(fd);

    return 0;
}
