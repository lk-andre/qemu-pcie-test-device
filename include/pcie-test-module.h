#ifndef PCIE_TEST_MODULE_H
#define PCIE_TEST_MODULE_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif // __KERNEL__

#ifdef USERSPACE_APP
#include <stdint.h>
#endif // USERSPACE_APP

#include <linux/ioctl.h>

typedef struct dma_ctrl {
    uint32_t op_code;
    uint32_t bytes;
    uint64_t src;
    uint64_t dst;
} dma_ctrl_t;

#define PCIE_TEST_IOCTL_PREFIX         'Z'
#define PCIE_TEST_IOCTL_DEVICE_VERSION _IOR(PCIE_TEST_IOCTL_PREFIX, 20, uint32_t)
#define PCIE_TEST_IOCTL_GET_STATUS     _IOR(PCIE_TEST_IOCTL_PREFIX, 21, uint32_t)
#define PCIE_TEST_IOCTL_GET_INT_STATUS _IOR(PCIE_TEST_IOCTL_PREFIX, 22, uint32_t)
#define PCIE_TEST_IOCTL_SET_INT_STATUS _IOW(PCIE_TEST_IOCTL_PREFIX, 23, uint32_t)
#define PCIE_TEST_IOCTL_SET_INT_MASK   _IOW(PCIE_TEST_IOCTL_PREFIX, 24, uint32_t)
#define PCIE_TEST_IOCTL_TEST_INT       _IOW(PCIE_TEST_IOCTL_PREFIX, 25, uint32_t)
#define PCIE_TEST_IOCTL_START_TRANSFER _IOW(PCIE_TEST_IOCTL_PREFIX, 29, dma_ctrl_t)

#endif /* PCIE_TEST_MODULE_H */
