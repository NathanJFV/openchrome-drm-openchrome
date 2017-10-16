/*
 * Copyright 2017 Kevin Brace. All Rights Reserved.
 * Copyright 2012 James Simmons <jsimmons@infradead.org>. All Rights Reserved.
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * This DRM's standby and resume code is based on Radeon DRM's code,
 * but it was shortened and simplified.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) OR COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <linux/module.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include <drm/via_drm.h>
#include <drm/drm_pciids.h>

#include "via_drv.h"

int via_modeset = 1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, via_modeset, int, 0400);

int via_hdmi_audio = 0;

MODULE_PARM_DESC(audio, "HDMI Audio enable (1 = enable)");
module_param_named(audio, via_hdmi_audio, int, 0444);

static struct pci_device_id via_pci_table[] = {
	viadrv_PCI_IDS,
};
MODULE_DEVICE_TABLE(pci, via_pci_table);

#define SGDMA_MEMORY (256*1024)
#define VQ_MEMORY (256*1024)

#if IS_ENABLED(CONFIG_AGP)

#define VIA_AGP_MODE_MASK	0x17
#define VIA_AGPV3_MODE		0x08
#define VIA_AGPV3_8X_MODE	0x02
#define VIA_AGPV3_4X_MODE	0x01
#define VIA_AGP_4X_MODE		0x04
#define VIA_AGP_2X_MODE		0x02
#define VIA_AGP_1X_MODE		0x01
#define VIA_AGP_FW_MODE		0x10

static int
via_detect_agp(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	struct drm_agp_info agp_info;
	struct drm_agp_mode mode;
	int ret = 0;

	ret = drm_agp_acquire(dev);
	if (ret) {
		DRM_ERROR("Failed acquiring AGP device.\n");
		return ret;
	}

	ret = drm_agp_info(dev, &agp_info);
	if (ret) {
		DRM_ERROR("Failed detecting AGP aperture size.\n");
		goto out_err0;
	}

	mode.mode = agp_info.mode & ~VIA_AGP_MODE_MASK;
	if (mode.mode & VIA_AGPV3_MODE)
		mode.mode |= VIA_AGPV3_8X_MODE;
	else
		mode.mode |= VIA_AGP_4X_MODE;

	mode.mode |= VIA_AGP_FW_MODE;
	ret = drm_agp_enable(dev, mode);
	if (ret) {
		DRM_ERROR("Failed to enable the AGP bus.\n");
		goto out_err0;
	}

	ret = ttm_bo_init_mm(&dev_priv->ttm.bdev, TTM_PL_TT, agp_info.aperture_size >> PAGE_SHIFT);
	if (!ret) {
		DRM_INFO("Detected %lu MB of AGP Aperture at "
			"physical address 0x%08lx.\n",
			agp_info.aperture_size >> 20,
			agp_info.aperture_base);
	} else {
out_err0:
		drm_agp_release(dev);
	}
	return ret;
}

static void via_agp_engine_init(struct via_device *dev_priv)
{
	VIA_WRITE(VIA_REG_TRANSET, 0x00100000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00333004);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x60000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x61000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x62000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x63000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x64000000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x7D000000);

	VIA_WRITE(VIA_REG_TRANSET, 0xfe020000);
	VIA_WRITE(VIA_REG_TRANSPACE, 0x00000000);
}
#endif

static void
via_mmio_setup(struct via_device *dev_priv)
{
	void __iomem *regs = ioport_map(0x3c0, 100);
	u8 val;

	DRM_INFO("Entered via_mmio_setup.\n");

	val = ioread8(regs + 0x03);
	iowrite8(val | 0x1, regs + 0x03);
	val = ioread8(regs + 0x0C);
	iowrite8(val | 0x1, regs + 0x02);

	/* Unlock Extended IO Space */
	iowrite8(0x10, regs + 0x04);
	iowrite8(0x01, regs + 0x05);
	/* Unlock CRTC register protect */
	iowrite8(0x47, regs + 0x14);

	/* Enable MMIO */
	iowrite8(0x1a, regs + 0x04);
	val = ioread8(regs + 0x05);
	iowrite8(val | 0x38, regs + 0x05);

    DRM_INFO("Exiting via_mmio_setup.\n");
}

static void
chip_revision_info(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	u8 tmp;

	switch (dev->pdev->device) {
	/* Check revision of CLE266 Chip */
	case PCI_DEVICE_ID_VIA_CLE266:
		/* CR4F only define in CLE266.CX chip */
		tmp = vga_rcrt(VGABASE, 0x4F);
		vga_wcrt(VGABASE, 0x4F, 0x55);
		if (vga_rcrt(VGABASE, 0x4F) != 0x55)
			dev_priv->revision = CLE266_REVISION_AX;
		else
			dev_priv->revision = CLE266_REVISION_CX;
		/* restore orignal CR4F value */
		vga_wcrt(VGABASE, 0x4F, tmp);
		break;

	case PCI_DEVICE_ID_VIA_VT3157:
		tmp = vga_rseq(VGABASE, 0x43);
		if (tmp & 0x02) {
			dev_priv->revision = CX700_REVISION_700M2;
		} else if (tmp & 0x40) {
			dev_priv->revision = CX700_REVISION_700M;
		} else {
			dev_priv->revision = CX700_REVISION_700;
		}
		break;

	case PCI_DEVICE_ID_VIA_VT1122:
	case PCI_DEVICE_ID_VIA_VX875:
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		dev_priv->revision = vga_rseq(VGABASE, 0x3B);
		break;

	default:
		break;
	}
}

static int via_dumb_create(struct drm_file *filp, struct drm_device *dev,
				struct drm_mode_create_dumb *args)
{
	struct via_device *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	int ret;

	args->pitch = round_up(args->width * (args->bpp >> 3), 16);
	args->size = args->pitch * args->height;
	obj = ttm_gem_create(dev, &dev_priv->ttm.bdev, args->size,
				ttm_bo_type_device, TTM_PL_FLAG_VRAM,
				16, PAGE_SIZE, false);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(filp, obj, &args->handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int via_dumb_mmap(struct drm_file *filp, struct drm_device *dev,
			uint32_t handle, uint64_t *offset_p)
{
	struct ttm_buffer_object *bo;
	struct drm_gem_object *obj;
	int rc = -ENOENT;

	obj = drm_gem_object_lookup(dev, filp, handle);
	if (obj == NULL)
		return rc;

	bo = ttm_gem_mapping(obj);
	if (bo != NULL) {
		*offset_p = drm_vma_node_offset_addr(&bo->vma_node);
		rc = 0;
	}
	drm_gem_object_unreference_unlocked(obj);
	return rc;
}

static int gem_dumb_destroy(struct drm_file *filp, struct drm_device *dev,
				uint32_t handle)
{
	return drm_gem_handle_delete(filp, handle);
}

static int
via_device_init(struct via_device *dev_priv)
{
    int ret;

    DRM_DEBUG("Entered via_device_init.\n");

    /* Temporary implementation. */
    dev_priv->is_via_nanobook = false;
    dev_priv->is_quanta_il1 = false;

    ret = via_vram_init(dev_priv);
    if (ret) {
        DRM_ERROR("Failed to initialize video RAM.\n");
        goto exit;
    }

    via_mmio_setup(dev_priv);

exit:
    DRM_DEBUG("Exiting via_device_init.\n");
    return ret;
}

static int via_driver_unload(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	struct ttm_buffer_object *bo;
	int ret = 0;

	ret = via_dma_cleanup(dev);
	if (ret)
		return ret;

	drm_irq_uninstall(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		via_modeset_fini(dev);

	via_fence_pool_fini(dev_priv->dma_fences);

	drm_vblank_cleanup(dev);

	/* destroy work queue. */
	if (dev_priv->wq)
		destroy_workqueue(dev_priv->wq);

	bo = dev_priv->vq.bo;
	if (bo) {
		via_bo_unpin(bo, &dev_priv->vq);
		ttm_bo_unref(&bo);
	}

	bo = dev_priv->gart.bo;
	if (bo) {
		/* enable gtt write */
		if (pci_is_pcie(dev->pdev))
			svga_wseq_mask(VGABASE, 0x6C, 0, BIT(7));
		via_bo_unpin(bo, &dev_priv->gart);
		ttm_bo_unref(&bo);
	}

	bo = dev_priv->mmio.bo;
	if (bo) {
		via_bo_unpin(bo, &dev_priv->mmio);
		ttm_bo_unref(&bo);
	}

	via_mm_fini(dev);

#if IS_ENABLED(CONFIG_AGP)
	if (dev->agp && dev->agp->acquired)
		drm_agp_release(dev);
#endif
	kfree(dev_priv);
	return ret;
}

static int
via_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct via_device *dev_priv;
	int ret = 0;

    DRM_INFO("Entered via_driver_load.\n");

    dev_priv = kzalloc(sizeof(struct via_device), GFP_KERNEL);
	if (!dev_priv) {
        ret = -ENOMEM;
        DRM_ERROR("Failed to allocate private storage memory.\n");
        goto exit;
	}

	dev->dev_private = (void *)dev_priv;
	dev_priv->engine_type = chipset;
	dev_priv->vram_mtrr = -ENXIO;
	dev_priv->dev = dev;

	via_init_command_verifier();

    ret = via_device_init(dev_priv);
    if (ret) {
        DRM_ERROR("Failed to initialize Chrome IGP.\n");
        goto init_error;
    }

    ret = via_mm_init(dev_priv);
	if (ret) {
        DRM_ERROR("Failed to initialize TTM.\n");
		goto init_error;
	}

	chip_revision_info(dev);

#if IS_ENABLED(CONFIG_AGP)
	if ((dev_priv->engine_type <= VIA_ENG_H2) ||
		(dev->agp && pci_find_capability(dev->pdev, PCI_CAP_ID_AGP))) {
		ret = via_detect_agp(dev);
		if (!ret)
			via_agp_engine_init(dev_priv);
		else
			DRM_ERROR("Failed to allocate AGP.\n");
	}
#endif
	if (pci_is_pcie(dev->pdev)) {
		/* Allocate GART. */
		ret = via_ttm_allocate_kernel_buffer(&dev_priv->ttm.bdev, SGDMA_MEMORY,
						16, TTM_PL_FLAG_VRAM,
						&dev_priv->gart);
		if (likely(!ret)) {
			DRM_INFO("Allocated %u KB of DMA memory.\n", SGDMA_MEMORY >> 10);
		} else {
			DRM_ERROR("Failed to allocate DMA memory.\n");
			goto init_error;
		}
	}

	/* Allocate VQ. (Virtual Queue) */
	ret = via_ttm_allocate_kernel_buffer(&dev_priv->ttm.bdev, VQ_MEMORY, 16,
					TTM_PL_FLAG_VRAM, &dev_priv->vq);
	if (likely(!ret)) {
		DRM_INFO("Allocated %u KB of VQ (Virtual Queue) memory.\n", VQ_MEMORY >> 10);
	} else {
		DRM_ERROR("Failed to allocate VQ (Virtual Queue) memory.\n");
		goto init_error;
	}

	via_engine_init(dev);

	/* Setting up a work queue. */
	dev_priv->wq = create_workqueue("viadrm");
	if (!dev_priv->wq) {
		DRM_ERROR("Failed to create a work queue.\n");
		ret = -EINVAL;
		goto init_error;
	}

	ret = drm_vblank_init(dev, 2);
	if (ret) {
        DRM_ERROR("Failed to initialize DRM VBlank.\n");
		goto init_error;
	}

	ret = via_dmablit_init(dev);
	if (ret) {
        DRM_ERROR("Failed to initialize DMA.\n");
		goto init_error;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = via_modeset_init(dev);
		if (ret) {
            DRM_ERROR("Failed to initialize mode setting.\n");
            goto init_error;
		}
	}

	ret = drm_irq_install(dev, dev->pdev->irq);
    if (ret) {
        DRM_ERROR("Failed to initialize DRM IRQ.\n");
        goto init_error;
    }

    goto exit;
init_error:
	if (ret)
		via_driver_unload(dev);
exit:
    DRM_INFO("Exiting via_driver_load.\n");
	return ret;
}

static int via_final_context(struct drm_device *dev, int context)
{
	/* Linux specific until context tracking code gets ported to BSD */
	/* Last context, perform cleanup */
	if (dev->dev_private) {
		DRM_DEBUG("Last Context\n");
		drm_irq_uninstall(dev);
		via_dma_cleanup(dev);
	}
	return 1;
}

static void via_driver_lastclose(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET) &&
	    dev->mode_config.funcs->output_poll_changed)
		dev->mode_config.funcs->output_poll_changed(dev);
}

static void via_reclaim_buffers_locked(struct drm_device *dev,
					struct drm_file *filp)
{
	mutex_lock(&dev->struct_mutex);
	via_driver_dma_quiescent(dev);
	mutex_unlock(&dev->struct_mutex);
	return;
}

static int via_pm_ops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct via_device *dev_priv = drm_dev->dev_private;

	DRM_DEBUG_KMS("Entered %s.", __func__);

	console_lock();
	if (dev_priv->via_fbdev->helper.fbdev) {
		fb_set_suspend(dev_priv->via_fbdev->helper.fbdev,
				true);
	}

	/* 3X5.3B through 3X5.3F are scratch pad registers.
	 * They are important for FP detection.
	 * Their values need to be saved because they get lost
	 * when resuming from standby. */
	dev_priv->saved_cr3b = vga_rcrt(VGABASE, 0x3b);
	dev_priv->saved_cr3c = vga_rcrt(VGABASE, 0x3c);
	dev_priv->saved_cr3d = vga_rcrt(VGABASE, 0x3d);
	dev_priv->saved_cr3e = vga_rcrt(VGABASE, 0x3e);
	dev_priv->saved_cr3f = vga_rcrt(VGABASE, 0x3f);

	console_unlock();

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return 0;
}

static int via_pm_ops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct via_device *dev_priv = drm_dev->dev_private;

	DRM_DEBUG_KMS("Entered %s.", __func__);

	console_lock();

	/* 3X5.3B through 3X5.3F are scratch pad registers.
	 * They are important for FP detection.
	 * Their values need to be restored because they are undefined
	 * after resuming from standby. */
	vga_wcrt(VGABASE, 0x3b, dev_priv->saved_cr3b);
	vga_wcrt(VGABASE, 0x3c, dev_priv->saved_cr3c);
	vga_wcrt(VGABASE, 0x3d, dev_priv->saved_cr3d);
	vga_wcrt(VGABASE, 0x3e, dev_priv->saved_cr3e);
	vga_wcrt(VGABASE, 0x3f, dev_priv->saved_cr3f);

	drm_helper_resume_force_mode(drm_dev);
	if (dev_priv->via_fbdev->helper.fbdev) {
		fb_set_suspend(dev_priv->via_fbdev->helper.fbdev,
				false);
	}

	console_unlock();

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return 0;
}

static const struct dev_pm_ops via_dev_pm_ops = {
	.suspend = via_pm_ops_suspend,
	.resume = via_pm_ops_resume,
};

static const struct file_operations via_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap		= ttm_mmap,
	.poll		= drm_poll,
	.llseek		= noop_llseek,
};

static struct drm_driver via_driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED |
					   DRIVER_GEM,
	.load = via_driver_load,
	.unload = via_driver_unload,
	.preclose = via_reclaim_buffers_locked,
	.context_dtor = via_final_context,
	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = via_enable_vblank,
	.disable_vblank = via_disable_vblank,
	.irq_preinstall = via_driver_irq_preinstall,
	.irq_postinstall = via_driver_irq_postinstall,
	.irq_uninstall = via_driver_irq_uninstall,
	.irq_handler = via_driver_irq_handler,
	.dma_quiescent = via_driver_dma_quiescent,
	.lastclose = via_driver_lastclose,
	.set_busid = drm_pci_set_busid,
	.gem_open_object = ttm_gem_open_object,
	.gem_free_object = ttm_gem_free_object,
	.dumb_create = via_dumb_create,
	.dumb_map_offset = via_dumb_mmap,
	.dumb_destroy = gem_dumb_destroy,
	.ioctls = via_ioctls,
	.fops = &via_driver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int
via_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_pci_dev(pdev, ent, &via_driver);
}

static void
via_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static struct pci_driver via_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= via_pci_table,
	.driver.pm	= &via_dev_pm_ops,
};

static int __init via_init(void)
{
	via_driver.num_ioctls = via_max_ioctl;

	if (via_modeset) {
		via_pci_driver.probe	= via_pci_probe;
		via_pci_driver.remove	= via_pci_remove;
		via_driver.driver_features |= DRIVER_MODESET;
	}
	return pci_register_driver(&via_pci_driver);
}

static void __exit via_exit(void)
{
	pci_unregister_driver(&via_pci_driver);
}

module_init(via_init);
module_exit(via_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
