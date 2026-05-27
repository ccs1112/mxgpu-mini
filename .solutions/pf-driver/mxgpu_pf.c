// SPDX-License-Identifier: GPL-2.0
/*
 * mxgpu-mini Physical Function driver.
 *
 * Phase 1: bind to the mxgpu-mini PCIe device, map BAR0 + BAR2, log
 * identification. Phase 2 adds SR-IOV (sriov_configure, VF spawn).
 * Phase 3 adds the world-switch scheduler.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define MXGPU_VENDOR_ID  0x1b36
#define MXGPU_DEVICE_ID  0x00e0

struct mxgpu_pf {
	struct pci_dev *pdev;
	void __iomem *mmio;
	void __iomem *scratch;
};

static int mxgpu_pf_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mxgpu_pf *pf;
	int ret;

	pf = devm_kzalloc(&pdev->dev, sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;
	pf->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0) | BIT(2), KBUILD_MODNAME);
	if (ret)
		return ret;

	pf->mmio    = pcim_iomap_table(pdev)[0];
	pf->scratch = pcim_iomap_table(pdev)[2];

	pci_set_drvdata(pdev, pf);
	pci_info(pdev, "mxgpu-mini PF probed (mmio=%p scratch=%p)\n",
		 pf->mmio, pf->scratch);
	return 0;
}

static void mxgpu_pf_remove(struct pci_dev *pdev)
{
	pci_info(pdev, "mxgpu-mini PF removed\n");
}

static const struct pci_device_id mxgpu_pf_ids[] = {
	{ PCI_DEVICE(MXGPU_VENDOR_ID, MXGPU_DEVICE_ID) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, mxgpu_pf_ids);

static struct pci_driver mxgpu_pf_driver = {
	.name     = "mxgpu_pf",
	.id_table = mxgpu_pf_ids,
	.probe    = mxgpu_pf_probe,
	.remove   = mxgpu_pf_remove,
};
module_pci_driver(mxgpu_pf_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("mxgpu-mini Physical Function driver");
