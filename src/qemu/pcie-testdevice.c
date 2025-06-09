/*
 * file : pcie-testdevice.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qom/object.h"

#include "qemu/log.h"

#include "hw/irq.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties-system.h"
#include "hw/qdev-properties.h"

#include "hw/misc/pcie_device_regs.h"

/* QEMU Device Definitions */
#define TYPE_PCIE_TEST_DEVICE        "pcie-test-device"
#define PCIE_TEST_DEVICE_VID         PCI_VENDOR_ID_QEMU
#define PCIE_TEST_DEVICE_DID         0xABBA
#define PCIE_TEST_DEVICE_REV         0x1
#define PCIE_TEST_DEVICE_CID         PCI_CLASS_MEMORY_RAM
#define PCIE_TEST_DEVICE_DESCRIPTION "PCIe Test Device"

/* Interrupts */
#define PCIE_TEST_DEVICE_INTERRUPT_PIN 1
#define PCIE_TEST_DEVICE_MSIX_VECTORS  1
#define PCIE_TEST_DEVICE_MSIX_BAR      3

/* Helpers */
#define INTERNAL_REG_OFFSET(i)  ((i) >> 2)
#define CTRL_REGS(reg, offset)  (reg[INTERNAL_REG_OFFSET(offset)])
#define DMA_REG(reg, i, offset) (reg[INTERNAL_REG_OFFSET(PCIE_TEST_DEVICE_DESC_OFFSET(i) + offset)])

/* Toggle to debug print */
//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) qemu_log("[%s]:" fmt, TYPE_PCIE_TEST_DEVICE, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

typedef struct PcieTestDevice {
    PCIDevice parentPci;

    /* PCIe BARs*/
    MemoryRegion bar0; /* BAR0 MMIO device registers */
    uint32_t regs[PCIE_TEST_DEVICE_MIMO_MAX_SIZE_DWORDS];

    MemoryRegion mem; /* BAR1 */

    /* Device Properties */
    PCIExpLinkSpeed speed;
    PCIExpLinkWidth width;
} PcieTestDevice;

OBJECT_DECLARE_SIMPLE_TYPE(PcieTestDevice, PCIE_TEST_DEVICE);

static const Property pcie_props[] = {
    DEFINE_PROP_PCIE_LINK_SPEED("x-speed", PcieTestDevice, speed, PCIE_LINK_SPEED_2_5),
    DEFINE_PROP_PCIE_LINK_WIDTH("x-width", PcieTestDevice, width, PCIE_LINK_WIDTH_16),
};

static void pcie_test_device_assert_interrupt(PcieTestDevice *dev)
{
    // Check if interrupt mask is enabled
    DeviceIntMask_t intMask = { .all = CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_INT_MASK_OFFSET) };
    if (intMask.bits.mask_0) {
        bool isMsixEnabled = msix_enabled(PCI_DEVICE(dev));
        if (isMsixEnabled) {
            msix_notify(PCI_DEVICE(dev), 0);
        } else {
            DEBUG_PRINT("%s - Trigger legacy interrupt\n", __func__);
            pci_irq_assert(PCI_DEVICE(dev));
        }
    }
}

static void pcie_test_device_start_transfer(PcieTestDevice *dev, const uint8_t descId, const bool isTrigger)
{
    DeviceCtrl_t ctrl = { .all = CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_CTRL_OFFSET) };
    if (!ctrl.bits.start) {
        return;
    }

    if (ctrl.bits.type > TEST_DEVICE_DMA_WRITE) {
        DEBUG_PRINT("%s - Invalid dma type!\n", __func__);
        return;
    }

    DEBUG_PRINT("%s - Setting up DMA transfer\n", __func__);

    PCIDevice *pci_dev = PCI_DEVICE(dev);
    dma_addr_t src_addr = ((dma_addr_t)DMA_REG(dev->regs, descId, PCIE_TEST_DEVICE_DESC_SRC_ADDR_HI) << 32)
                          | ((dma_addr_t)DMA_REG(dev->regs, descId, PCIE_TEST_DEVICE_DESC_SRC_ADDR_LOW));
    dma_addr_t dst_addr = ((dma_addr_t)DMA_REG(dev->regs, descId, PCIE_TEST_DEVICE_DESC_DST_ADDR_HI) << 32)
                          | ((dma_addr_t)DMA_REG(dev->regs, descId, PCIE_TEST_DEVICE_DESC_DST_ADDR_LOW));
    dma_addr_t dma_len = DMA_REG(dev->regs, descId, PCIE_TEST_DEVICE_DESC_TX_SIZE);

    DEBUG_PRINT("%s - Descriptor src: 0x%" PRIx64 ", dst 0x%" PRIx64 ", "
                "len %" PRIu64 "\n",
                __func__, src_addr, dst_addr, dma_len);

    // Set device busy
    DeviceStatus_t deviceStatus = { .all = CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET) };
    deviceStatus.bits.busy_0 = 1;
    CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET) = deviceStatus.all;

    MemTxResult dmaResult = MEMTX_OK;
    volatile uint8_t *pRam = memory_region_get_ram_ptr(&dev->mem);
    switch (ctrl.bits.type) {
    case TEST_DEVICE_DMA_READ: {
        assert((dst_addr + dma_len) < PCIE_TEST_DEVICE_BUFF_SIZE_BYTES);
        DEBUG_PRINT("%s - Performing DMA host -> device\n", __func__);

        dmaResult = pci_dma_read(pci_dev, src_addr, (void *)&pRam[dst_addr], dma_len);
        if (dmaResult != MEMTX_OK) {
            DEBUG_PRINT("%s - Transfer failed with status %" PRIu32 "\n", __func__, dmaResult);
        }
    } break;
    case TEST_DEVICE_DMA_WRITE: {
        assert((src_addr + dma_len) < PCIE_TEST_DEVICE_BUFF_SIZE_BYTES);
        DEBUG_PRINT("%s - Performing DMA device -> host\n", __func__);

        dmaResult = pci_dma_write(pci_dev, dst_addr, (void *)&pRam[src_addr], dma_len);
        if (dmaResult != MEMTX_OK) {
            DEBUG_PRINT("%s - Transfer failed with status %" PRIu32 "\n", __func__, dmaResult);
        }
    } break;
    default:
        break;
    }

    // Signal completion and update registers
    DEBUG_PRINT("%s - Update completion registers\n", __func__);
    deviceStatus.bits.busy_0 = 0;
    CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET) = deviceStatus.all;

    // Early exit since DMA failed
    if (dmaResult != MEMTX_OK) {
        return;
    }

    // Update interrupt status
    DeviceIntStatus_t intStatus = { .all = CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET) };
    intStatus.bits.int_0 = 1;
    CTRL_REGS(dev->regs, PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET) = intStatus.all;

    pcie_test_device_assert_interrupt(dev);
}

static bool mmio_address_in_range(hwaddr addr)
{
    bool inCtrlRange = (addr <= PCIE_TEST_DEVICE_MMIO_LAST_ADDR);

    bool inDescRange = false;
    for (uint8_t idx = 0; idx < PCIE_TEST_DEVICE_NUM_DESC; idx++) {
        inDescRange |= (addr >= PCIE_TEST_DEVICE_DESC_OFFSET(idx)
                        && addr <= (PCIE_TEST_DEVICE_DESC_OFFSET(idx) + PCIE_TEST_DEVICE_DESC_LAST_ADDR));
    }
    return inCtrlRange | inDescRange;
}

static void mmio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    PCIDevice *pci_dev = PCI_DEVICE(opaque);
    PcieTestDevice *d = PCIE_TEST_DEVICE(opaque);

    DEBUG_PRINT(":%s - addr 0x%" PRIx64 ", value 0x%" PRIx64 "\n", __func__, addr, value);

    if (!mmio_address_in_range(addr)) {
        DEBUG_PRINT("%s - Invalid register address!\n", __func__);
        return;
    }

    switch (addr) {
    case PCIE_TEST_DEVICE_MMIO_CTRL_OFFSET: {
        DeviceCtrl_t ctrl = { .all = value };
        CTRL_REGS(d->regs, addr) = ctrl.all;
        pcie_test_device_start_transfer(d, 0, ctrl.bits.start & ~ctrl.bits.reset);

        // Always clear start, reset bit
        ctrl.bits.start = 0;
        ctrl.bits.reset = 0;
        CTRL_REGS(d->regs, addr) = ctrl.all;
    } break;
    case PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET: {
        DEBUG_PRINT("%s - Clearing interrupt status and deassert IRQ\n", __func__);
        // Clear interrupt on write
        CTRL_REGS(d->regs, addr) = CTRL_REGS(d->regs, addr) & ~value;

        bool isMsixEnabled = msix_enabled(PCI_DEVICE(pci_dev));
        if (!isMsixEnabled) {
            pci_irq_deassert(pci_dev);
        }
    } break;
    case PCIE_TEST_DEVICE_MMIO_INT_TRIGGER_OFFSET: {
        DEBUG_PRINT("%s - Trigger interrupt status and assert IRQ\n", __func__);

        DeviceIntStatus_t intStatus = { .all = CTRL_REGS(d->regs, PCIE_TEST_DEVICE_MMIO_INT_MASK_OFFSET) };
        intStatus.bits.int_0 = 1;
        CTRL_REGS(d->regs, PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET) = intStatus.all;

        // Check if interrupt mask is enabled
        pcie_test_device_assert_interrupt(d);
    } break;
    case PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET:
    case PCIE_TEST_DEVICE_MMIO_VER_OFFSET: {
        // Do nothing since this should be RO
    } break;
    default: {
        CTRL_REGS(d->regs, addr) = value;
    } break;
    }
}

static uint64_t mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    DEBUG_PRINT("%s - addr 0x%" PRIx64 "\n", __func__, addr);

    if (!mmio_address_in_range(addr)) {
        DEBUG_PRINT("%s - Invalid register address!\n", __func__);
        return UINT64_MAX;
    }

    PcieTestDevice *d = PCIE_TEST_DEVICE(opaque);
    return CTRL_REGS(d->regs, addr);
}

static const MemoryRegionOps bar_ops = {
    .read = mmio_read,
    .write = mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static void pcie_test_device_reset_regs_and_mem(DeviceState *qdev, const bool isScrubRam)
{
    PcieTestDevice *d = PCIE_TEST_DEVICE(qdev);

    DEBUG_PRINT("%s - Clearing control registers\n", __func__);
    for (uint8_t idx = 0; idx <= PCIE_TEST_DEVICE_MMIO_LAST_ADDR; idx += sizeof(uint32_t)) {
        CTRL_REGS(d->regs, idx) = 0;
    }
    CTRL_REGS(d->regs, PCIE_TEST_DEVICE_MMIO_VER_OFFSET) = PCI_TEST_DEVICE_IP_VERSION;

    for (uint8_t idx = 0; idx < PCIE_TEST_DEVICE_NUM_DESC; idx++) {
        DMA_REG(d->regs, idx, PCIE_TEST_DEVICE_DESC_SRC_ADDR_HI) = 0;
        DMA_REG(d->regs, idx, PCIE_TEST_DEVICE_DESC_SRC_ADDR_LOW) = 0;
        DMA_REG(d->regs, idx, PCIE_TEST_DEVICE_DESC_DST_ADDR_HI) = 0;
        DMA_REG(d->regs, idx, PCIE_TEST_DEVICE_DESC_DST_ADDR_LOW) = 0;
        DMA_REG(d->regs, idx, PCIE_TEST_DEVICE_DESC_TX_SIZE) = 0;
    }

    if (isScrubRam) {
        DEBUG_PRINT("%s - Scrub device RAM with incrementing pattern\n", __func__);

        volatile uint8_t *pRam = memory_region_get_ram_ptr(&d->mem);
        for (uint32_t idx = 0; idx < PCIE_TEST_DEVICE_BUFF_SIZE_BYTES; idx++) {
            pRam[idx] = (idx & UINT8_MAX);
        }
    }
}

static void pcie_test_device_init(Object *obj) { DEBUG_PRINT("%s - Initialize device object\n", __func__); }

static void pcie_test_device_realize(PCIDevice *pci_dev, Error **errp)
{
    PcieTestDevice *d = PCIE_TEST_DEVICE(pci_dev);

    DEBUG_PRINT("%s - Realizing device\n", __func__);

    // Setup BARs from 0 - PCI_NUM_REGIONS-1
    // Register callbacks to BAR0 mmio region
    memory_region_init_io(&d->bar0, OBJECT(d), &bar_ops, d, "pcie-test-device-bar0",
                          PCIE_TEST_DEVICE_MIMO_MAX_SIZE_BYTES);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->bar0);

    // Initialize device memory region
    memory_region_init_ram(&d->mem, OBJECT(d), "pcie-test-deve-bar1", PCIE_TEST_DEVICE_BUFF_SIZE_BYTES, errp);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mem);

    // Reset registers and write a test pattern
    pcie_test_device_reset_regs_and_mem(&(pci_dev->qdev), true);

    // Set up interrupt for IntA
    pci_config_set_interrupt_pin(pci_dev->config, PCIE_TEST_DEVICE_INTERRUPT_PIN);

    // MSI-X state is managed internally in PCIDevice
    if (msix_init_exclusive_bar(pci_dev, PCIE_TEST_DEVICE_MSIX_VECTORS, PCIE_TEST_DEVICE_MSIX_BAR, errp)) {
        DEBUG_PRINT("%s - Failed to initialize MSI-X\n", __func__);
        assert(false);
    }
    for (uint32_t i = 0; i < PCIE_TEST_DEVICE_MSIX_VECTORS; i++) {
        msix_vector_use(pci_dev, i);
    }

    if (!pci_bus_is_express(pci_get_bus(pci_dev))) {
        assert(false);
    }

    // Will end up as a RC integrated EP
    // https://gitlab.com/qemu-project/qemu/-/blob/v10.0.0/hw/pci/pcie.c?ref_type=tags#L290
    pcie_endpoint_cap_init(pci_dev, 0);
    // pcie_cap_init(pci_dev, 0, PCI_EXP_TYPE_ENDPOINT, 0, errp);
    // pcie_cap_fill_link_ep_usp(pci_dev, d->width, d->speed);
}

static void pcie_test_device_reset(DeviceState *qdev) { DEBUG_PRINT("%s - Reset device\n", __func__); }

static void pcie_test_device_finalize(Object *object) { DEBUG_PRINT("%s - Finalize device\n", __func__); }

static void pcie_test_device_exit(PCIDevice *pci_dev)
{
    DEBUG_PRINT("%s - Exit cleanup\n", __func__);
    pcie_cap_exit(pci_dev);
    msix_uninit_exclusive_bar(pci_dev);
}

static void pcie_test_device_class_init(ObjectClass *klass, void *data)
{
    DEBUG_PRINT("%s - Initialize device class\n", __func__);

    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pcic = PCI_DEVICE_CLASS(klass);

    // Realize, exit
    pcic->realize = pcie_test_device_realize;
    pcic->exit = pcie_test_device_exit;

    // Device reset
    device_class_set_legacy_reset(dc, pcie_test_device_reset);

    // Skip defining state serialisation

    // Setup common PCI config space
    pcic->vendor_id = PCIE_TEST_DEVICE_VID;
    pcic->device_id = PCIE_TEST_DEVICE_DID;
    pcic->revision = PCIE_TEST_DEVICE_REV;
    pcic->class_id = PCIE_TEST_DEVICE_CID;

    pcic->subsystem_vendor_id = 0x0;
    pcic->subsystem_id = 0x0;

    // Device description
    dc->desc = PCIE_TEST_DEVICE_DESCRIPTION;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    device_class_set_props(dc, pcie_props);
}

// Define PCIE_TEST_DEVICE object creation
// https://gitlab.com/qemu-project/qemu/-/blob/v10.0.0/include/qom/object.h?ref_type=tags#L476
static const TypeInfo pcie_test_device_info = {
    .name = TYPE_PCIE_TEST_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    // Instant object definitions
    .instance_size = sizeof(PcieTestDevice),
    .instance_init = pcie_test_device_init,
    .instance_finalize = pcie_test_device_finalize,
    // Class definitions
    .class_init = pcie_test_device_class_init,
    // Specify PCIe interface for this device
    .interfaces =
        (InterfaceInfo[]){
            {INTERFACE_PCIE_DEVICE},
            {},
        },
};

static void pcie_test_device_register_types(void) { type_register_static(&pcie_test_device_info); }

// Register function to run before running `main`
type_init(pcie_test_device_register_types)
