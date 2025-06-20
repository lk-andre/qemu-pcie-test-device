#include <inttypes.h>
#include <stdio.h>

#include <assert.h>

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pcie-test-module.h"

#define CHAR_DEVICE_PATH "/dev/%s"

static const uint32_t EXPECTED_VERSION = 0x0101;
static const uint32_t BUFFER_SIZE_BYTES = 0x10000;

static uint32_t poll_interrupt(int fd);
static int test_dma_transfer(int fd, const dma_ctrl_t *dma_ctrl, uint32_t *irq_count);

int main(int argc, char *argv[])
{
    uint32_t init_irq_count;
    int status;
    uint32_t version, int_status, int_mask, value;
    dma_ctrl_t dma_ctrl = { 0 };

    char charDevice[512];

    if (argc < 2) {
        fprintf(stderr, "ERROR: Missing character device name!\n");
        return 1;
    }

    snprintf(charDevice, sizeof(charDevice), CHAR_DEVICE_PATH, argv[1]);

    printf("Running Kernel module tests\n");

    int fd = open(charDevice, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s\n", charDevice);
        return 1;
    }

    printf("--- Checking Version Register ---\n");
    if (ioctl(fd, PCIE_TEST_IOCTL_DEVICE_VERSION, &version) < 0) {
        fprintf(stderr, "ERROR: Failed to get device version!\n");
        return 2;
    }
    assert(version == EXPECTED_VERSION);

    printf("--- Testing Force Trigger Interrupt ---\n");
    printf("Checking Interrupt Status First");
    int_status = 0x1;
    if (ioctl(fd, PCIE_TEST_IOCTL_SET_INT_STATUS, &int_status) < 0) {
        fprintf(stderr, "ERROR: Failed to write to interrupt status!\n");
        return 3;
    }
    if (ioctl(fd, PCIE_TEST_IOCTL_GET_INT_STATUS, &int_status) < 0) {
        fprintf(stderr, "ERROR: Failed to read the interrupt status!\n");
        return 4;
    }
    // Interrupt Status should be 0 before the test
    assert(int_status == 0);

    int_mask = 0x1;
    if (ioctl(fd, PCIE_TEST_IOCTL_SET_INT_MASK, &int_mask) < 0) {
        fprintf(stderr, "ERROR: Failed to write to interrupt mask register!\n");
        return 5;
    }

    printf("- Force Interrupt\n");
    value = 0x1;
    if (ioctl(fd, PCIE_TEST_IOCTL_TEST_INT, &value) < 0) {
        fprintf(stderr, "ERROR: Failed to force interrupt!\n");
        return 6;
    }

    init_irq_count = poll_interrupt(fd);

    if (ioctl(fd, PCIE_TEST_IOCTL_GET_INT_STATUS, &int_status) < 0) {
        fprintf(stderr, "ERROR: Failed to read the interrupt status!\n");
        return 4;
    }
    assert(int_status == 0x0);

    printf("--- Testing Device Memory ---\n");
    void *buf = mmap(NULL, BUFFER_SIZE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "ERROR: Failed to mmap!\n");
        close(fd);
        return 1;
    }

    memset(buf, 0, BUFFER_SIZE_BYTES);
    printf("Writing 32 bytes (decrementing pattern) to DMA buffer @ 0x0 from userspace...\n");
    printf("Writing 32 bytes (incrementing pattern) to DMA buffer @ 0x120 from userspace...\n");
    for (uint32_t idx = 0; idx < 32; idx++) {
        *((uint8_t *)buf + 0 + idx) = (0xFF - idx);
        *((uint8_t *)buf + 0x120 + idx) = idx;
    }

    dma_ctrl.op_code = 0;
    dma_ctrl.src = 0x0;
    dma_ctrl.dst = 0x0;
    dma_ctrl.bytes = 32;
    printf("Transfer contents from DMA buffer to device (%" PRIu32 " bytes @ 0x%" PRIx64 " to 0x%" PRIx64 ")\n",
           dma_ctrl.bytes, dma_ctrl.src, dma_ctrl.dst);
    assert(test_dma_transfer(fd, &dma_ctrl, &init_irq_count) == 0);

    dma_ctrl.op_code = 0;
    dma_ctrl.src = 0x120;
    dma_ctrl.dst = 0x120;
    dma_ctrl.bytes = 32;
    printf("Transfer contents from DMA buffer to device (%" PRIu32 " bytes @ 0x%" PRIx64 " to 0x%" PRIx64 ")\n",
           dma_ctrl.bytes, dma_ctrl.src, dma_ctrl.dst);
    assert(test_dma_transfer(fd, &dma_ctrl, &init_irq_count) == 0);

    dma_ctrl.op_code = 1;
    dma_ctrl.src = 0x0;
    dma_ctrl.dst = 0x120;
    dma_ctrl.bytes = 32;
    printf("Transfer contents from device to DMA buffer (%" PRIu32 " bytes @ 0x%" PRIx64 " to 0x%" PRIx64 ")\n",
           dma_ctrl.bytes, dma_ctrl.src, dma_ctrl.dst);
    assert(test_dma_transfer(fd, &dma_ctrl, &init_irq_count) == 0);

    dma_ctrl.op_code = 1;
    dma_ctrl.src = 0x120;
    dma_ctrl.dst = 0x0;
    dma_ctrl.bytes = 32;
    printf("Transfer contents from device to DMA buffer (%" PRIu32 " bytes @ 0x%" PRIx64 " to 0x%" PRIx64 ")\n",
           dma_ctrl.bytes, dma_ctrl.src, dma_ctrl.dst);
    assert(test_dma_transfer(fd, &dma_ctrl, &init_irq_count) == 0);

    dma_ctrl.op_code = 1;
    dma_ctrl.src = 0x0;
    dma_ctrl.dst = 0xfff0;
    dma_ctrl.bytes = 16;
    printf("Transfer contents device to DMA buffer (%" PRIu32 " bytes @ 0x%" PRIx64 " to 0x%" PRIx64 ")\n",
           dma_ctrl.bytes, dma_ctrl.src, dma_ctrl.dst);
    assert(test_dma_transfer(fd, &dma_ctrl, &init_irq_count) == 0);

    printf("Checking buffer content\n");

    // Buffer @ 0x0 should have incrementing pattern
    uint32_t buf_offset = 0;
    uint32_t num_check_bytes = 32;
    for (uint32_t idx = 0; idx < 2 * num_check_bytes; idx++) {
        volatile uint8_t *pRegAddr = ((uint8_t *)buf + buf_offset + idx);
        if (idx < num_check_bytes) {
            if (*pRegAddr != idx) {
                fprintf(stderr, "ERROR: Mismatch data @ 0x%" PRIx32 " - 0x%" PRIx8 " vs 0x%" PRIx8 "\n",
                        buf_offset + idx, idx, *pRegAddr);
            }
        } else {
            // Outside the region shouldn't be incrementing based on location selection
            if (*pRegAddr != 0) {
                fprintf(stderr, "ERROR: Mismatch data @ 0x%" PRIx32 ", should not be 0x%" PRIx8 "\n", buf_offset + idx,
                        *pRegAddr);
            }
        }
    }

    // Buffer @ 0x120 should have decrementing pattern
    buf_offset = 0x120;
    num_check_bytes = 32;
    for (uint32_t idx = 0; idx < 2 * num_check_bytes; idx++) {
        volatile uint8_t *pRegAddr = ((uint8_t *)buf + buf_offset + idx);
        if (idx < num_check_bytes) {
            if (*pRegAddr != (0xFF - idx)) {
                fprintf(stderr, "ERROR: Mismatch data @ 0x%" PRIx32 " - 0x%" PRIx8 " vs 0x%" PRIx8 "\n",
                        buf_offset + idx, (0xFF - idx), *pRegAddr);
            }
        } else {
            // Outside the region shouldn't be the expected decrementing based on location selection
            if (*pRegAddr != 0) {
                fprintf(stderr, "ERROR: Mismatch data @ 0x%" PRIx32 ", should not be 0x%" PRIx8 "\n", buf_offset + idx,
                        *pRegAddr);
            }
        }
    }

    // Buffer @ 0xfff0 should have incrementing pattern
    buf_offset = 0xfff0;
    num_check_bytes = 16;
    for (uint32_t idx = 0; idx < num_check_bytes; idx++) {
        volatile uint8_t *pRegAddr = ((uint8_t *)buf + buf_offset + idx);
        if (*pRegAddr != (0xFF - idx)) {
            fprintf(stderr, "ERROR: Mismatch data @ 0x%" PRIx32 " - 0x%" PRIx8 " vs 0x%" PRIx8 "\n", buf_offset + idx,
                    (0xFF - idx), *pRegAddr);
        }
    }

    munmap(buf, BUFFER_SIZE_BYTES);
    close(fd);

    printf("Kernel module tests passed \xE2\x9C\x93!\n");

    return 0;
}

static uint32_t poll_interrupt(int fd)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };

    poll(&pfd, 1, -1); // wait forever
    if (pfd.revents & POLLIN) {
        uint32_t irq_count = 0;

        ssize_t bytes = read(fd, &irq_count, sizeof(irq_count));
        if (bytes != sizeof(irq_count)) {
            printf("ERROR: Error occured during read\n");
        }
        return irq_count;
    }
    return 8;
}

static int test_dma_transfer(int fd, const dma_ctrl_t *dma_ctrl, uint32_t *init_irq_count)
{
    if (ioctl(fd, PCIE_TEST_IOCTL_START_TRANSFER, dma_ctrl) < 0) {
        fprintf(stderr, "ERROR: Failed to start DMA transfer!\n");
        return 7;
    }

    uint32_t irq_count = poll_interrupt(fd);
    assert(irq_count == (*init_irq_count + 1));
    *init_irq_count = irq_count;

    return 0;
}
