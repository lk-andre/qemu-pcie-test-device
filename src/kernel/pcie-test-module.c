
#include "pcie-test-module.h"

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <asm/page.h>

#include "pcie_device_regs.h"

/* ============================================================
 *                         PCI SPECIFIC
 * ============================================================ */
#include <linux/pci.h>

#define PCIE_TEST_DRIVER_VERSION "1.0"

MODULE_AUTHOR("andre");
MODULE_DESCRIPTION("Module for PCIe test device");
MODULE_LICENSE("GPL");
MODULE_VERSION(PCIE_TEST_DRIVER_VERSION);

#define PCIE_TEST_DEVICE_VID 0x1234
#define PCIE_TEST_DEVICE_DID 0xABBA

// Table of Device IDs supported by this driver
static struct pci_device_id pcie_id_table[] = {
    { PCI_DEVICE(PCIE_TEST_DEVICE_VID, PCIE_TEST_DEVICE_DID) },
    {},
};
MODULE_DEVICE_TABLE(pci, pcie_id_table);

#define PCIE_TEST_KERNEL_DRIVER_NAME "pcie-test-device"
#define PCIE_TEST_DEVICE_DEVICE_NAME "pcietest"
#define PCIE_TEST_DEVICE_CLASS_NAME  "pcietestclass"
#define PCIE_TEST_DEVICE_NUM         1
#define PCIE_TEST_DEVICE_MINOR_COUNT 1

typedef struct pcie_device {
    char name[512];

    struct pci_dev *pdev;

    struct cdev cdev;
    struct device *device;

    /* BAR0 MMIO registers */
    void __iomem *bar0_mmio;

    /* DMA Related */
    size_t alloc_size;
    void *virt_addr;
    dma_addr_t phys_addr;

    /* Interrupt Related */
    uint32_t irq_count;

    dev_t dev_number;
    int minor;
} pcie_device_t;

static struct class *g_pcie_class = NULL;
static dev_t g_base_dev;
static DEFINE_IDA(g_device_ida);

static atomic_t irq_event = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(wait_queue);

static int log_level = 0;
module_param(log_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(log_level, "Enable debug logging (0:off)");

static int pcie_open(struct inode *inode, struct file *file);
static int pcie_release(struct inode *inode, struct file *file);
static long pcie_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int pcie_mmap(struct file *file, struct vm_area_struct *vma);
static ssize_t pcie_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static unsigned int pcie_poll(struct file *file, poll_table *wait);

/* Define Attributes */
static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t status;
    status = sprintf(buf, "%s\n", PCIE_TEST_DRIVER_VERSION);
    return status;
}

static struct device_attribute dev_pcie_attrs[] = {
    __ATTR_RO(version),
    __ATTR_NULL,
};

static struct attribute *pcie_test_attrs[] = {
    &dev_pcie_attrs[0].attr,
    NULL,
};

ATTRIBUTE_GROUPS(pcie_test);

// Interrupt handler function
static irqreturn_t intHandlerHard(int irq, void *pdev)
{
    pcie_device_t *pcie_device = (pcie_device_t *)pdev;
    struct device *dev = pcie_device->device;

    dev_dbg(dev, "%s - Entered Handler\n", __func__);
    const uint32_t value = readl(pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET);
    if (value) {
        pcie_device->irq_count++;
        writel(value, pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET);

        atomic_set(&irq_event, 1);
        wake_up_interruptible(&wait_queue);
    }
    return IRQ_HANDLED;
}

/**
 * Device file operations
 */
static const struct file_operations g_device_file_ops = {
    .owner = THIS_MODULE,
    .open = pcie_open,
    .release = pcie_release,
    .mmap = pcie_mmap,
    .unlocked_ioctl = pcie_ioctl,
    .read = pcie_read,
    .poll = pcie_poll,
};

static unsigned int pcie_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &wait_queue, wait);
    if (atomic_read(&irq_event)) {
        return POLLIN | POLLRDNORM;
    }
    return 0;
}

static ssize_t pcie_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    pcie_device_t *pcie_device = file->private_data;
    struct device *dev = &pcie_device->pdev->dev;

    wait_event_interruptible(wait_queue, atomic_read(&irq_event) != 0);
    atomic_set(&irq_event, 0);

    uint32_t irq_count = pcie_device->irq_count;
    if (count < sizeof(irq_count)) {
        return -EINVAL;
    }

    if (copy_to_user(buf, &irq_count, sizeof(irq_count)))
        return -EFAULT;

    if (log_level) {
        dev_info(dev, "%s - Returning IRQ Count %u\n", __func__, irq_count);
    }

    return sizeof(irq_count);
}

static int pcie_open(struct inode *inode, struct file *file)
{
    pcie_device_t *pcie_device = container_of(inode->i_cdev, pcie_device_t, cdev);
    file->private_data = pcie_device;
    return 0;
}

static int pcie_release(struct inode *inode, struct file *file) { return 0; }

static long pcie_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    pcie_device_t *pcie_device = file->private_data;
    struct device *dev = pcie_device->device;

    long result = -EFAULT;
    switch (cmd) {
    case PCIE_TEST_IOCTL_DEVICE_VERSION: {
        const uint32_t value = readl(pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_VER_OFFSET);
        if (copy_to_user((uint32_t *)arg, &value, sizeof(value))) {
            return -EFAULT;
        } else {
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_GET_STATUS: {
        const uint32_t value = readl(pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_STATUS_OFFSET);
        if (copy_to_user((uint32_t *)arg, &value, sizeof(value))) {
            result = -EFAULT;
        } else {
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_GET_INT_STATUS: {
        const uint32_t value = readl(pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET);
        if (copy_to_user((uint32_t *)arg, &value, sizeof(value))) {
            result = -EFAULT;
        } else {
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_SET_INT_STATUS: {
        uint32_t value = 0;
        if (copy_from_user(&value, (uint32_t *)arg, sizeof(value)) != 0) {
            result = -EFAULT;
        } else {
            writel(value, pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_STATUS_OFFSET);
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_SET_INT_MASK: {
        uint32_t value = 0;
        if (copy_from_user(&value, (uint32_t *)arg, sizeof(value)) != 0) {
            result = -EFAULT;
        } else {
            writel(value, pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_MASK_OFFSET);
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_TEST_INT: {
        uint32_t value = 0;
        if (copy_from_user(&value, (uint32_t *)arg, sizeof(value)) != 0) {
            result = -EFAULT;
        } else {
            writel(value, pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_INT_TRIGGER_OFFSET);
            result = 0;
        }
    } break;
    case PCIE_TEST_IOCTL_START_TRANSFER: {
        result = 0;
        dma_ctrl_t value = { 0 };
        if (copy_from_user(&value, (uint32_t *)arg, sizeof(dma_ctrl_t)) != 0) {
            dev_err(dev, "%s - Failed to parse instruction!\n", __func__);
            result = -EFAULT;
        }

        if (pcie_device->virt_addr == NULL) {
            dev_err(dev, "%s - Invalid dma buf address!\n", __func__);
            result = -EFAULT;
        }

        if (value.op_code > 1) {
            dev_err(dev, "%s - Invalid op code (%u)!\n", __func__, value.op_code);
            result = -EFAULT;
        }

        if (result == 0) {
            uint32_t descOffset = PCIE_TEST_DEVICE_DESC_OFFSET(0);

            if (log_level) {
                dev_info(dev, "%s - Performing transfer. Op Code %u, Src 0x%llx, dst 0x%llx, size %u\n", __func__,
                         value.op_code, value.src, value.dst, value.bytes);
            }

            uint64_t final_dst_addr = value.dst;
            uint64_t final_src_addr = value.src;
            if (value.op_code == 0) {
                final_src_addr = value.src + pcie_device->phys_addr;
            } else {
                final_dst_addr = value.dst + pcie_device->phys_addr;
            }
            writel(((uint64_t)final_src_addr >> 32) & U32_MAX,
                   pcie_device->bar0_mmio + descOffset + PCIE_TEST_DEVICE_DESC_SRC_ADDR_HI);
            writel(((uint64_t)final_src_addr) & U32_MAX,
                   pcie_device->bar0_mmio + descOffset + PCIE_TEST_DEVICE_DESC_SRC_ADDR_LOW);
            writel(((uint64_t)final_dst_addr >> 32) & U32_MAX,
                   pcie_device->bar0_mmio + descOffset + PCIE_TEST_DEVICE_DESC_DST_ADDR_HI);
            writel(((uint64_t)final_dst_addr) & U32_MAX,
                   pcie_device->bar0_mmio + descOffset + PCIE_TEST_DEVICE_DESC_DST_ADDR_LOW);
            writel(value.bytes, pcie_device->bar0_mmio + descOffset + PCIE_TEST_DEVICE_DESC_TX_SIZE);

            DeviceCtrl_t ctrl = { 0 };
            ctrl.bits.start = 1;
            ctrl.bits.type = value.op_code;
            writel(ctrl.all, pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_CTRL_OFFSET);
            result = 0;
        }
    } break;
    default:
        break;
    }
    return result;
}

static int pcie_mmap(struct file *file, struct vm_area_struct *vma)
{
    pcie_device_t *pcie_device = file->private_data;
    struct device *dev = pcie_device->device;

    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn = virt_to_phys(pcie_device->virt_addr) >> PAGE_SHIFT;

    if (size > pcie_device->alloc_size)
        return -EINVAL;

    dev_dbg(dev, "%s - mapping DMA buffer to userspace (size: %lu, pfn: 0x%lx)\n", __func__, size, pfn);

    return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static int pcie_module_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
    int err;
    pcie_device_t *pcie_device;
    resource_size_t start_addr, len_bytes;
    struct device *dev = &pdev->dev;

    dev_dbg(dev, "%s - PCI device detected: %04x:%04x\n", __func__, pid->vendor, pid->device);
    /*
        Allocate memory for device structure
        https://www.kernel.org/doc/html/next/core-api/memory-allocation.html
        GFP  - get free pages
     */
    pcie_device = kzalloc(sizeof(pcie_device_t), GFP_KERNEL);
    if (pcie_device == NULL) {
        return -ENOMEM;
    }

    snprintf(pcie_device->name, sizeof(pcie_device->name), PCIE_TEST_KERNEL_DRIVER_NAME "%d", 0);
    pcie_device->pdev = pdev;
    pci_set_drvdata(pdev, pcie_device);

    dev_dbg(dev, "%s - Enabling PCIe Device\n", __func__);
    /* This will :
     *  - wake up the device if it was in suspended state,
     *  - allocate I/O and memory regions of the device (if BIOS did not),
     *  - allocate an IRQ (if BIOS did not).
     */
    err = pci_enable_device(pdev);
    if (err) {
        dev_err(dev, "%s - error %d, enable device failed!\n", __func__, err);
        goto pci_enable_device_fail;
    }

    // Request MMIO/IOP resources
    err = pci_request_regions(pdev, pcie_device->name);
    if (err) {
        dev_err(dev, "%s - error %d, failed to request for pci region!\n", __func__, err);
        goto pci_request_region_fail;
    }

    // Request BAR0: control registers
    start_addr = pci_resource_start(pdev, 0);
    len_bytes = pci_resource_len(pdev, 0);
    pcie_device->bar0_mmio = ioremap(start_addr, len_bytes);
    if (!pcie_device->bar0_mmio) {
        dev_err(dev, "%s - cannot ioremap registers of size %lu\n", __func__, (unsigned long)len_bytes);
        goto ioremap_fail;
    }
    dev_dbg(dev, "%s - BAR0 (start: 0x%lx, len: %llu)\n", __func__, (unsigned long)pcie_device->bar0_mmio, len_bytes);

    // Set the DMA mask size (for both coherent and streaming DMA)
    err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
    if (err) {
        dev_err(dev, "%s - error %d, failed to set dma mask\n", __func__, err);
        goto dma_set_mask_and_coherent_fail;
    }

    /* Allocate and initialize shared control data (pci_allocate_coherent()) */

    // Access device configuratin space
    dev_dbg(dev, "%s - read version information: 0x%x\n", __func__,
            readl(pcie_device->bar0_mmio + PCIE_TEST_DEVICE_MMIO_VER_OFFSET));

    // Request IRQ handler
    dev_dbg(dev, "%s - Allocate interrupt vector\n", __func__);
    err = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
    if (err < 0) {
        dev_err(dev, "%s - error %d, failed to allocate irq vector\n", __func__, err);
        goto pci_alloc_irq_vectors_fail;
    }

    // Get IRQ number for vector 0
    int irq = pci_irq_vector(pdev, 0);
    if (log_level) {
        dev_info(dev, "%s - Enable interrupt and register handler to IRQ %u\n", __func__, irq);
    }
    err = request_threaded_irq(irq, intHandlerHard, NULL, IRQF_SHARED, "PCIe test device interrupt", pcie_device);
    if (err) {
        dev_err(dev, "%s - Failed to enable interrupt\n", __func__);
        goto request_threaded_irq_fail;
    }

    // Enable DMA/processing engines
    dev_dbg(dev, "%s - Enable bus mastering\n", __func__);
    pci_set_master(pdev);

    // DMA buffer allocation
    pcie_device->alloc_size = (((size_t)0x10000 + (((size_t)1 << PAGE_SHIFT) - 1)) >> PAGE_SHIFT) << PAGE_SHIFT;
    pcie_device->virt_addr = dma_alloc_coherent(dev, pcie_device->alloc_size, &pcie_device->phys_addr, GFP_KERNEL);
    if (log_level) {
        dev_info(dev, "%s - Allocated buffer - virt addr: %p, phy addr: 0x%llx\n", __func__, pcie_device->virt_addr,
                 pcie_device->phys_addr);
    }

    // Allocate unique ID for device
    pcie_device->minor = ida_alloc(&g_device_ida, GFP_KERNEL);
    pcie_device->dev_number = MKDEV(MAJOR(g_base_dev), pcie_device->minor);

    // Create device interface
    cdev_init(&pcie_device->cdev, &g_device_file_ops);
    pcie_device->cdev.owner = THIS_MODULE;
    err = cdev_add(&pcie_device->cdev, pcie_device->dev_number, PCIE_TEST_DEVICE_MINOR_COUNT);
    if (err < 0) {
        dev_err(dev, "%s - Failed to add char dev\n", __func__);
        goto cdev_add_fail;
    }
    pcie_device->device = device_create(g_pcie_class, NULL, pcie_device->dev_number, pcie_device, "%s%d",
                                        PCIE_TEST_DEVICE_DEVICE_NAME, pcie_device->minor);

    if (log_level) {
        dev_info(dev, "%s - Probe Complete\n", __func__);
    }
    return 0;

cdev_add_fail:
    ida_free(&g_device_ida, pcie_device->minor);
    dma_free_coherent(&pdev->dev, pcie_device->alloc_size, pcie_device->virt_addr, pcie_device->phys_addr);
    pci_clear_master(pdev);

request_threaded_irq_fail:
    pci_free_irq_vectors(pdev);

pci_alloc_irq_vectors_fail:
dma_set_mask_and_coherent_fail:
ioremap_fail:
    if (pcie_device->bar0_mmio) {
        iounmap(pcie_device->bar0_mmio);
    }

pci_request_region_fail:
    pci_release_regions(pdev);
    pci_disable_device(pdev);

pci_enable_device_fail:
    pci_set_drvdata(pdev, NULL);
    kfree(pcie_device);
    return err;
}

static void pcie_module_remove(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;

    dev_dbg(dev, "%s - Disable bus mastering\n", __func__);
    pci_clear_master(pdev);

    pcie_device_t *pcie_device = pci_get_drvdata(pdev);
    if (pcie_device != NULL) {
        if (pcie_device->virt_addr != NULL) {
            dev_dbg(dev, "%s - Freeing DMA Buffers\n", __func__);
            dma_free_coherent(&pdev->dev, pcie_device->alloc_size, pcie_device->virt_addr, pcie_device->phys_addr);
            pcie_device->virt_addr = NULL;
        }
        int irq = pci_irq_vector(pdev, 0);
        dev_dbg(dev, "%s - Remove interrupt handler for IRQ %u\n", __func__, irq);
        free_irq(irq, pcie_device);
    }
    pci_free_irq_vectors(pdev);

    dev_dbg(dev, "%s - Remving device\n", __func__);
    device_destroy(g_pcie_class, pcie_device->dev_number);

    dev_dbg(dev, "%s - Removing char device\n", __func__);
    cdev_del(&pcie_device->cdev);

    dev_dbg(dev, "%s - Free IDA\n", __func__);
    ida_free(&g_device_ida, pcie_device->minor);

    dev_dbg(dev, "%s - Release PCIe resources\n", __func__);
    iounmap(pcie_device->bar0_mmio);
    pci_release_regions(pdev);

    dev_dbg(dev, "%s - Disable PCIe device\n", __func__);
    pci_disable_device(pdev);

    pci_set_drvdata(pdev, NULL);
    kfree(pcie_device);

    dev_dbg(dev, "%s - Complete\n", __func__);
}

static struct pci_driver pcie_module_driver = {
    .name = PCIE_TEST_KERNEL_DRIVER_NAME,
    .id_table = pcie_id_table,
    .probe = pcie_module_probe,
    .remove = pcie_module_remove,
};

static int __init pcie_test_module_init(void)
{
    int err;
    pr_debug("%s - Attempting to insert module\n", __func__);

    // Allocate a character device number
    err = alloc_chrdev_region(&g_base_dev, 0, PCIE_TEST_DEVICE_NUM, PCIE_TEST_DEVICE_DEVICE_NAME);
    if (err < 0) {
        pr_err("%s - Failed to allocate char device\n", __func__);
        goto alloc_chrdev_region_fail;
    }

    g_pcie_class = class_create(THIS_MODULE, PCIE_TEST_DEVICE_CLASS_NAME);
    if (IS_ERR_OR_NULL(g_pcie_class)) {
        err = PTR_ERR(g_pcie_class);
        g_pcie_class = NULL;
        pr_err("%s - Failed to create class\n", __func__);
        goto class_create_fail;
    }

    g_pcie_class->dev_groups = pcie_test_groups;

    ida_init(&g_device_ida);

    pr_debug("%s - Registering PCIe driver\n", __func__);
    err = pci_register_driver(&pcie_module_driver);
    if (err) {
        pr_err("%s - Failed to register PCIe driver\n", __func__);
        goto pci_register_driver_fail;
    }

    pr_debug("%s - Module insertion complete\n", __func__);
    return 0;

pci_register_driver_fail:
    if (g_pcie_class) {
        class_destroy(g_pcie_class);
    }
    unregister_chrdev_region(g_base_dev, PCIE_TEST_DEVICE_NUM);

class_create_fail:
alloc_chrdev_region_fail:
    return err;
}

static void __exit pcie_test_module_exit(void)
{
    pr_debug("%s - Attempting to remove module\n", __func__);

    pci_unregister_driver(&pcie_module_driver);

    ida_destroy(&g_device_ida);

    if (g_pcie_class) {
        class_destroy(g_pcie_class);
    }

    unregister_chrdev_region(g_base_dev, PCIE_TEST_DEVICE_NUM);

    pr_debug("%s - Module removal complete\n", __func__);
}

module_init(pcie_test_module_init);
module_exit(pcie_test_module_exit);
