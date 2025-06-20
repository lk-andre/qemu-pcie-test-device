#ifndef PCIE_DEVICE_REGS_H
#define PCIE_DEVICE_REGS_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif // __KERNEL__

#ifdef USERSPACE_APP
#include <assert.h>
#include <stdint.h>
#endif // USERSPACE_APP

#define PCI_TEST_DEVICE_IP_VERSION            0x0101

#define PCIE_TEST_DEVICE_NUM_DESC             1
#define PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES  0x1000
#define PCIE_TEST_DEVICE_MIMO_MAX_SIZE_DWORDS (PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES / 4)

/* BAR0 MMIO register */
#define PCIE_TEST_DEVICE_MMIO_CTRL_OFFSET        0x0000
#define PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET      0x0004
#define PCIE_TEST_DEVICE_MMIO_INT_MASK_OFFSET    0x0008
#define PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET  0x000C
#define PCIE_TEST_DEVICE_MMIO_INT_TRIGGER_OFFSET 0x0010
#define PCIE_TEST_DEVICE_MMIO_SCRATCH_OFFSET     0x0014
#define PCIE_TEST_DEVICE_MMIO_VER_OFFSET         0x0018
#define PCIE_TEST_DEVICE_MMIO_LAST_ADDR          PCIE_TEST_DEVICE_MMIO_VER_OFFSET

enum DmaType_e {
    TEST_DEVICE_DMA_READ = 0x0,
    TEST_DEVICE_DMA_WRITE = 0x1,
};

// Register definition for ctrl register
typedef union __attribute__((packed)) {
    struct {
        uint32_t start : 1;
        uint32_t type : 2;
        uint32_t reserved_0 : 28;
        uint32_t reset : 1;
    } bits;
    uint32_t all;
} DeviceCtrl_t;

// Register definition for status register
typedef union __attribute__((packed)) {
    struct {
        uint32_t busy_0 : 1;
        uint32_t reserved_1 : 31;
    } bits;
    uint32_t all;
} DeviceStatus_t;

typedef union __attribute__((packed)) {
    struct {
        uint32_t mask_0 : 1;
        uint32_t reserved_0 : 31;
    } bits;
    uint32_t all;
} DeviceIntMask_t;

typedef union __attribute__((packed)) {
    struct {
        uint32_t int_0 : 1;
        uint32_t reserved_0 : 31;
    } bits;
    uint32_t all;
} DeviceIntStatus_t;

typedef union __attribute__((packed)) {
    struct {
        uint32_t trigger_0 : 1;
        uint32_t reserved_0 : 31;
    } bits;
    uint32_t all;
} DeviceTrigger_t;

typedef union __attribute__((packed)) {
    struct {
        uint32_t minor : 8;
        uint32_t major : 8;
        uint32_t reserved_0 : 16;
    } bits;
    uint32_t all;
} DeviceVersion_t;

/* Descriptor register */

// Toy example with 1 descriptor
#define PCIE_TEST_DEVICE_DESC_BASE_OFFSET  0x0020
#define PCIE_TEST_DEVICE_DESC_OFFSET(i)    (PCIE_TEST_DEVICE_DESC_BASE_OFFSET + (i)*PCIE_TEST_DEVICE_DESC_SIZE)
#define PCIE_TEST_DEVICE_DESC_SIZE         0x0040

#define PCIE_TEST_DEVICE_DESC_SRC_ADDR_HI  0x0000
#define PCIE_TEST_DEVICE_DESC_SRC_ADDR_LOW 0x0004
#define PCIE_TEST_DEVICE_DESC_DST_ADDR_HI  0x0008
#define PCIE_TEST_DEVICE_DESC_DST_ADDR_LOW 0x000C
#define PCIE_TEST_DEVICE_DESC_TX_SIZE      0x0010
#define PCIE_TEST_DEVICE_DESC_LAST_ADDR    (PCIE_TEST_DEVICE_DESC_BASE_OFFSET + PCIE_TEST_DEVICE_DESC_TX_SIZE)

typedef struct {
    uint32_t ctrl;       // Nothing defined here
    uint32_t srcAddrHi;  // Source Address[63:32]
    uint32_t srcAddrLow; // Source Address[31:0]
    uint32_t dstAddrHi;  // Destination Address[63:32]
    uint32_t dstAddrLow; // Destination Address[31:0]
    uint32_t txSize;
} DmaDescriptor_t;

#define PCIE_TEST_DEVICE_BUFF_SIZE_BYTES 0x10000

static_assert(PCIE_TEST_DEVICE_MMIO_VER_OFFSET < PCIE_TEST_DEVICE_DESC_BASE_OFFSET,
              "Descriptor within control register range");
static_assert((PCIE_TEST_DEVICE_DESC_OFFSET(PCIE_TEST_DEVICE_NUM_DESC - 1) + PCIE_TEST_DEVICE_DESC_LAST_ADDR)
                  <= PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES,
              "Descriptor exceeds max range");

#endif // PCIE_DEVICE_REGS_H
