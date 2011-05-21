/*
 * Copyright 2011 James Simmons <jsimmons@infradead.org>. All Rights Reserved.
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
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "via_drm.h"
#include "via_drv.h"

#include "drm_pciids.h"
#include "drm_fb_helper.h"
#include "drm_crtc_helper.h"

static int
cle266_mem_type(struct drm_via_private *dev_priv, struct pci_dev *bridge)
{
	u8 type, fsb, freq;
	int ret;

	ret = pci_read_config_byte(bridge, 0x54, &fsb);
	if (ret)
		return ret;
	ret = pci_read_config_byte(bridge, 0x69, &freq);
	if (ret)
		return ret;

	freq >>= 6;
	fsb >>= 6;

	/* FSB frequency */
	switch (fsb) {
	case 0x01: /* 100MHz */
		switch (freq) {
		case 0x00:
			freq = 100;
			break;
		case 0x01:
			freq = 133;
			break;
		case 0x02:
			freq = 66;
			break;
		default:
			freq = 0;
			break;
		}
		break;

	case 0x02: /* 133 MHz */
	case 0x03:
		switch (freq) {
		case 0x00:
			freq = 133;
			break;
		case 0x02:
			freq = 100;
			break;
		default:
			freq = 0;
			break;
		}
		break;
	default:
		freq = 0;
		break;
	}

	ret = pci_read_config_byte(bridge, 0x60, &fsb);
	if (ret)
		return ret;
	ret = pci_read_config_byte(bridge, 0xE3, &type);
	if (ret)
		return ret;

	/* On bank 2/3 */
	if (type & 0x02)
		fsb >>= 2;

	/* Memory type */
	switch (fsb & 0x03) {
	case 0x00: /* SDR */
		switch (freq) {
		case 66:
			dev_priv->vram_type = VIA_MEM_SDR66;
			break;
		case 100:
			dev_priv->vram_type = VIA_MEM_SDR100;
			break;
		case 133:
			dev_priv->vram_type = VIA_MEM_SDR133;
		default:
			break;
		}
		break;

	case 0x02: /* DDR */
		switch (freq) {
		case 100:
			dev_priv->vram_type = VIA_MEM_DDR_200;
			break;
		case 133:
			dev_priv->vram_type = VIA_MEM_DDR_266;
		default:
			break;
		}
	default:
		break;
	}
	return ret;
}

static int
km400_mem_type(struct drm_via_private *dev_priv, struct pci_dev *bridge)
{
	u8 fsb, freq, rev;
	int ret;

	ret = pci_read_config_byte(bridge, 0xF6, &rev);
	if (ret)
		return ret;
	ret = pci_read_config_byte(bridge, 0x54, &fsb);
	if (ret)
		return ret;
	ret = pci_read_config_byte(bridge, 0x69, &freq);
	if (ret)
		return ret;

	freq >>= 6;
	fsb >>= 6;

	/* KM400 */
	if (rev < 0x80) {
		/* FSB frequency */
		switch (fsb) {
		case 0x00:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_200;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR_266;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR_400;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR_333;
			default:
				break;
			}
			break;

		case 0x01:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_266;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR_400;
			default:
				break;
			}
			break;

		case 0x02:
		case 0x03:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR_400;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR_266;	
			default:
				break;
			}
		default:
			break;
		}
	} else {
		/* KM400A */
		ret = pci_read_config_byte(bridge, 0x67, &rev);
		if (ret)
			return ret;
		if (rev & 0x80)
			freq |= 0x04;

		switch (fsb) {
		case 0x00:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_200;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR_266;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x07:
				dev_priv->vram_type = VIA_MEM_DDR_400;
				break;
			default:
				dev_priv->vram_type = VIA_MEM_NONE;
				break;
			}
			break;

		case 0x01:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_266;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR_400;
			default:
				break;
			}
			break;

		case 0x02:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_400;
				break;
			case 0x04:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x06:
				dev_priv->vram_type = VIA_MEM_DDR_266;
			default:
				break;
			}
			break;

		case 0x03:
			switch (freq) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR_400;
				break;
			case 0x04:
				dev_priv->vram_type = VIA_MEM_DDR_266;
			default:
				break;
			}
		default:
			break;
		}
	}
	return ret;
}

static int
p4m800_mem_type(struct drm_via_private *dev_priv, struct pci_bus *bus,
		struct pci_dev *fn3)
{
	struct pci_dev *fn4 = pci_get_slot(bus, PCI_DEVFN(0, 4));
	int ret, freq = 0;
	u8 type, fsb;

	/* VIA Scratch region */
	ret = pci_read_config_byte(fn4, 0xF3, &fsb);
	if (ret) {
		pci_dev_put(fn4);
		return ret;
	}

	switch (fsb >> 5) {
	case 0:
		freq = 3; /* 100 MHz */
		break;	
	case 1:
		freq = 4; /* 133 MHz */
		break;
	case 3:
		freq = 5; /* 166 MHz */
		break;
	case 2:
		freq = 6; /* 200 MHz */
		break;
	case 4:
		freq = 7; /* 233 MHz */
	default:
		break;
	}
	pci_dev_put(fn4);

	ret = pci_read_config_byte(fn3, 0x68, &type);
	if (ret)
		return ret;
	type &= 0x0f;

	if (type & 0x02)
		freq -= type >> 2;
	else {
		freq += type >> 2;
		if (type & 0x01)
			freq++;
	}

	switch (freq) {
	case 0x03:
		dev_priv->vram_type = VIA_MEM_DDR_200;
		break;
	case 0x04:
		dev_priv->vram_type = VIA_MEM_DDR_266;	
		break;
	case 0x05:
		dev_priv->vram_type = VIA_MEM_DDR_333;
		break;
	case 0x06:
		dev_priv->vram_type = VIA_MEM_DDR_400;
	default:
		break;
	}
	return ret;	
}

static int
km8xx_mem_type(struct drm_via_private *dev_priv)
{
	struct pci_dev *dram, *misc;
	int ret = -ENXIO;
	u8 type, tmp;

	dram = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MEMCTL, NULL);
	if (dram) {
		misc = pci_get_device(PCI_VENDOR_ID_AMD,
					PCI_DEVICE_ID_AMD_K8_NB_MISC, NULL);

		ret = pci_read_config_byte(misc, 0xFD, &type);
		if (type) {
			pci_read_config_byte(dram, 0x94, &type);
			switch (type & 0x03) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR2_400;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR2_533;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR2_667;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR2_800;
			default:
				break;
			}
		} else {
			ret = pci_read_config_byte(dram, 0x96, &type);
			if (ret)
				return ret; 
			type >>= 4;
			type &= 0x07;

			switch (type) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR_200;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR_266;
				break;
			case 0x05:
				dev_priv->vram_type = VIA_MEM_DDR_333;
				break;
			case 0x07:
				dev_priv->vram_type = VIA_MEM_DDR_400;
			default:
				break;
			}
		}
	}

	/* AMD 10h DRAM Controller */
	dram = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_DRAM, NULL);
	if (dram) {
		ret = pci_read_config_byte(misc, 0x94, &tmp);
		if (ret)
			return ret;
		ret = pci_read_config_byte(misc, 0x95, &type);
		if (ret)
			return ret;

        	if (type & 0x01) {	/* DDR3 */
			switch(tmp & 0x07) {
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR3_800;
				break;
			case 0x04:
				dev_priv->vram_type = VIA_MEM_DDR3_1066;
				break;
			case 0x05:
				dev_priv->vram_type = VIA_MEM_DDR3_1333;
				break;
			case 0x06:
				dev_priv->vram_type = VIA_MEM_DDR3_1600;
			default:
				break;
			}
        	} else {		/* DDR2 */
			switch(tmp & 0x07) {
			case 0x00:
				dev_priv->vram_type = VIA_MEM_DDR2_400;
				break;
			case 0x01:
				dev_priv->vram_type = VIA_MEM_DDR2_533;
				break;
			case 0x02:
				dev_priv->vram_type = VIA_MEM_DDR2_667;
				break;
			case 0x03:
				dev_priv->vram_type = VIA_MEM_DDR2_800;
				break;
			case 0x04:
				dev_priv->vram_type = VIA_MEM_DDR2_1066;
			default:
				break;
			}
		}
	}

	/* AMD 11h DRAM Controller */
	dram = pci_get_device(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_11H_NB_DRAM, NULL);
	if (dram) {
		ret = pci_read_config_byte(misc, 0x94, &type);
		if (ret)
			return ret;

		switch(tmp & 0x07) {
		case 0x01:
			dev_priv->vram_type = VIA_MEM_DDR2_533;
			break;
		case 0x02:
			dev_priv->vram_type = VIA_MEM_DDR2_667;
			break;
		case 0x03:
			dev_priv->vram_type = VIA_MEM_DDR2_800;
		default:
			break;
		}
	}
	return ret;
}

static int
cn400_mem_type(struct drm_via_private *dev_priv, struct pci_bus *bus,
		struct pci_dev *fn3)
{
	struct pci_dev *fn2 = pci_get_slot(bus, PCI_DEVFN(0, 2));
	int ret, freq = 0;
	u8 type, fsb;

	ret = pci_read_config_byte(fn2, 0x54, &fsb);
	if (ret) {
		pci_dev_put(fn2);
		return ret;
	}

	switch (fsb >> 5) {
	case 0:
		freq = 3; /* 100 MHz */
		break;	
	case 1:
		freq = 4; /* 133 MHz */
		break;
	case 3:
		freq = 5; /* 166 MHz */
		break;
	case 2:
		freq = 6; /* 200 MHz */
		break;
	case 4:
		freq = 7; /* 233 MHz */
	default:
		break;
	}
	pci_dev_put(fn2);

	ret = pci_read_config_byte(fn3, 0x68, &type);
	if (ret)
		return ret;
	type &= 0x0f;

	if (type & 0x01)
		freq += 1 + (type >> 2);
	else
		freq -= 1 + (type >> 2);

	switch (freq) {
	case 0x03:
		dev_priv->vram_type = VIA_MEM_DDR_200;
		break;
	case 0x04:
		dev_priv->vram_type = VIA_MEM_DDR_266;	
		break;
	case 0x05:
		dev_priv->vram_type = VIA_MEM_DDR_333;
		break;
	case 0x06:
		dev_priv->vram_type = VIA_MEM_DDR_400;
	default:
		break;
	}
	return ret;
}

static int
cn700_mem_type(struct drm_via_private *dev_priv, struct pci_dev *fn3)
{
	int ret;
	u8 tmp;
	
	ret = pci_read_config_byte(fn3, 0x90, &tmp);
	if (!ret) {
		switch(tmp & 0x07) {
		case 0x00:
			dev_priv->vram_type = VIA_MEM_DDR_200;
			break;
		case 0x01:
			dev_priv->vram_type = VIA_MEM_DDR_266;
			break;
		case 0x02:
			dev_priv->vram_type = VIA_MEM_DDR_333;
			break;
		case 0x03:
			dev_priv->vram_type = VIA_MEM_DDR_400;
			break;
		case 0x04:
			dev_priv->vram_type = VIA_MEM_DDR2_400;
			break;
		case 0x05:
			dev_priv->vram_type = VIA_MEM_DDR2_533;
		default:
			break;
		}
	}
	return ret;
}

static int
cx700_mem_type(struct drm_via_private *dev_priv, struct pci_dev *fn3)
{
	u8 type, clock;
	int ret;

	ret = pci_read_config_byte(fn3, 0x90, &clock);
	if (ret)
		return ret;
	ret = pci_read_config_byte(fn3, 0x6C, &type);
	if (ret)
		return ret;
	type &= 0x40;
	type >>= 6;

	switch (type) {
	case 0:
		switch (clock) {
		case 0:
			dev_priv->vram_type = VIA_MEM_DDR_200;
			break;
		case 1:
			dev_priv->vram_type = VIA_MEM_DDR_266;
			break;
		case 2:
			dev_priv->vram_type = VIA_MEM_DDR_333;
			break;
		case 3:
			dev_priv->vram_type = VIA_MEM_DDR_400;
		default:
			break;
		}
		break;

	case 1:
		switch (clock) {
		case 3:
			dev_priv->vram_type = VIA_MEM_DDR2_400;
			break;
		case 4:
			dev_priv->vram_type = VIA_MEM_DDR2_533;
		default:
			break;
		}
	default:
		break;
	}
	return ret;
}

int via_detect_vram(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = dev->dev_private;
	struct pci_dev *bridge = NULL, *fn3 = NULL;
	u8 type = VIA_MEM_NONE, size;
	int vram_size = 0, ret = 0;
	char *name = "Unknown";
	struct pci_bus *bus;

	bus = pci_find_bus(0, 0);
	if (bus == NULL) {
		ret = -EINVAL;
		goto out_err;
	}

	bridge = pci_get_slot(bus, PCI_DEVFN(0, 0));
	fn3 = pci_get_slot(bus, PCI_DEVFN(0, 3));

	if (!bridge) {
		ret = -EINVAL;
		DRM_ERROR("No host bridge found...\n");
		goto out_err;
	}
	dev_priv->bridge_id = bridge->device;	

	if (!fn3 && dev->pci_device != PCI_DEVICE_ID_VIA_CLE266
		&& dev->pci_device != PCI_DEVICE_ID_VIA_KM400) {
		ret = -EINVAL;
		DRM_ERROR("No function 3 on host bridge...\n");
		goto out_err;
	}

	switch (bridge->device) {

	/* CLE266 */
	case PCI_DEVICE_ID_VIA_862X_0:
		ret = cle266_mem_type(dev_priv, bridge);
		if (ret)
			goto out_err;

		ret = pci_read_config_byte(bridge, 0xE1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;
		break;

	/* KM400/KN400 */
	case PCI_DEVICE_ID_VIA_8378_0:
		ret = km400_mem_type(dev_priv, bridge);

		ret = pci_read_config_byte(bridge, 0xE1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;
		break;

	/* P4M800 */
	case PCI_DEVICE_ID_VIA_3296_0:
		type = p4m800_mem_type(dev_priv, bus, fn3);		

		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;
		break;

	/* K8M800/K8N800 */
	case PCI_DEVICE_ID_VIA_8380_0: 
	/* K8M890 */
	case PCI_DEVICE_ID_VIA_VT3336:
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;

		if (bridge->device == PCI_DEVICE_ID_VIA_VT3336)
			vram_size <<= 2;
		
		ret = km8xx_mem_type(dev_priv);
		if (ret)
			goto out_err;
		break;

	/* CN400/PM800/PM880 */
	case PCI_DEVICE_ID_VIA_PX8X0_0:
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;

		ret = cn400_mem_type(dev_priv, bus, fn3);
		if (ret)
			goto out_err;
		break;

	/* CN700/VN800/P4M800CE/P4M800Pro */
	case PCI_DEVICE_ID_VIA_P4M800CE:
	/* P4M900/VN896/CN896 */
	case PCI_DEVICE_ID_VIA_VT3364:
	/* VX800 */
	case PCI_DEVICE_ID_VIA_VT3353:
	/* VX855 */
	case PCI_DEVICE_ID_VIA_VT3409:
	/* VX900 */
	case PCI_DEVICE_ID_VIA_VT3410:
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 20;

		if (bridge->device != PCI_DEVICE_ID_VIA_P4M800CE)
			vram_size <<= 2;

		ret = cn700_mem_type(dev_priv, fn3);
		if  (ret)
			goto out_err;
		break;

	/* CX700/VX700 */
	case PCI_DEVICE_ID_VIA_VT3324:
	/* P4M890 */
	case PCI_DEVICE_ID_VIA_P4M890:
		ret = pci_read_config_byte(fn3, 0xA1, &size);
		if (ret)
			goto out_err;
		vram_size = (1 << ((size & 0x70) >> 4)) << 22;

		ret = cx700_mem_type(dev_priv, fn3);
		if (ret)
			goto out_err;
		break;

	default:
		DRM_ERROR("Unknown North Bridge device 0x%04x.\n", bridge->device);
		goto out_err;
	}

	/*
	 * Detect VRAM start.
	 */
	if (fn3 != NULL && (fn3->device == 0x3204)) {
		pci_read_config_byte(fn3, 0x47, &size);
		dev_priv->vram_start = size << 24;
		dev_priv->vram_start -= vram_size;
	} else {
		int index = (bridge->device == PCI_DEVICE_ID_VIA_VT3410) ? 2 : 0;

		dev_priv->vram_start = pci_resource_start(dev->pdev, index);
	}

	switch (dev_priv->vram_type) {
	case VIA_MEM_SDR66:
		name = "SDR 66";
		break;
	case VIA_MEM_SDR100:
		name = "SDR 100";
		break;
	case VIA_MEM_SDR133:
		name = "SDR 133";
		break;
	case VIA_MEM_DDR_200:
		name = "DDR 200";
		break;
	case VIA_MEM_DDR_266:
		name = "DDR 266";
		break;
	case VIA_MEM_DDR_333:
		name = "DDR 333";
		break;
	case VIA_MEM_DDR_400:
		name = "DDR 400";
		break;
	case VIA_MEM_DDR2_400:
		name = "DDR2 400";
		break;
	case VIA_MEM_DDR2_533:
		name = "DDR2 533";
		break;
	case VIA_MEM_DDR2_667:
		name = "DDR2 667";
		break;
	case VIA_MEM_DDR2_800:
		name = "DDR2 800";
		break;
	case VIA_MEM_DDR2_1066:
		name = "DDR2 1066";
		break;
	default:
		break;
	}

	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_VRAM, vram_size >> PAGE_SHIFT);
	if (!ret) {
		DRM_INFO("Detected %llu MB of %s Video RAM at physical address 0x%08llx.\n",
			(unsigned long long) vram_size >> 20, name,
			(unsigned long long) dev_priv->vram_start);
	}
out_err:
	if (bridge)
		pci_dev_put(bridge);
	if (fn3)
		pci_dev_put(fn3);
	return ret;
}

static int
via_user_framebuffer_create_handle(struct drm_framebuffer *fb,
					struct drm_file *file_priv,
					unsigned int *handle)
{
	struct drm_gem_object *obj = fb->helper_private;

	return drm_gem_handle_create(file_priv, obj, handle);
}

static void
via_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct drm_gem_object *obj = fb->helper_private;

	if (obj) {
		drm_gem_object_unreference_unlocked(obj);
		fb->dev->driver->gem_free_object(obj);
	}
	drm_framebuffer_cleanup(fb);
}

static const struct drm_framebuffer_funcs via_fb_funcs = {
	.create_handle	= via_user_framebuffer_create_handle,
	.destroy	= via_user_framebuffer_destroy,
};

static void
via_output_poll_changed(struct drm_device *dev)
{
}

static struct drm_framebuffer *
via_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
	if (obj ==  NULL) {
		DRM_ERROR("No GEM object available to create framebuffer\n");
		return ERR_PTR(-ENOENT);
	}

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL)
		return ERR_PTR(-ENOMEM);

	fb->helper_private = obj;
	ret = drm_framebuffer_init(dev, fb, &via_fb_funcs);
	if (ret)
		return ERR_PTR(ret);

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);
	return fb;
}

static const struct drm_mode_config_funcs via_mode_funcs = {
	.fb_create		= via_user_framebuffer_create,
	.output_poll_changed	= via_output_poll_changed
};

static int 
via_fb_probe(struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct drm_via_private *dev_priv = helper->dev->dev_private;
	struct fb_info *info = helper->fbdev;
	struct ttm_bo_kmap_obj *kmap = NULL;
	struct drm_framebuffer *fb = NULL;
	struct drm_gem_object *obj = NULL; 
	struct drm_mode_fb_cmd mode_cmd;
	int size, ret = 0;
	void *ptr;

	/* Already exist */
	if (helper->fb)
		return ret;

	size = sizeof(*fb) + sizeof(*kmap);
	ptr = kzalloc(size, GFP_KERNEL);
	if (ptr == NULL)
		return -ENOMEM;
	fb = ptr;
	kmap = ptr + sizeof(*fb);

	mode_cmd.height = sizes->surface_height;
	mode_cmd.width = sizes->surface_width;
	mode_cmd.depth = sizes->surface_depth;
	mode_cmd.bpp = sizes->surface_bpp;

	mode_cmd.pitch = ((mode_cmd.width * mode_cmd.bpp >> 3) + 7) & ~7;
	size = mode_cmd.pitch * mode_cmd.height;

	obj = drm_gem_object_alloc(helper->dev, size);
	ret = ttm_bo_allocate(&dev_priv->bdev, size, ttm_bo_type_kernel,
				TTM_PL_FLAG_VRAM, 1, PAGE_SIZE, 0, false,
				via_ttm_bo_destroy, obj->filp, &kmap->bo);
	if (ret)
		goto out_err;
	obj->driver_private = kmap->bo;

	ret = ttm_bo_reserve(kmap->bo, true, false, false, 0);
	if (!ret) {
		ret = ttm_bo_kmap(kmap->bo, 0, kmap->bo->num_pages, kmap);
		ttm_bo_unreserve(kmap->bo);
	}
	if (ret)
		goto out_err;

	ret = drm_framebuffer_init(helper->dev, fb, &via_fb_funcs);
	if (ret)
		goto out_err;
	fb->helper_private = obj;

	drm_helper_mode_fill_fb_struct(fb, &mode_cmd);
	helper->helper_private = kmap;
	helper->fb = fb;

	info->fix.smem_start = kmap->bo->mem.bus.base +
				kmap->bo->mem.bus.offset;
	info->fix.smem_len = kmap->bo->num_pages << PAGE_SHIFT;
	info->screen_size = kmap->bo->num_pages << PAGE_SHIFT;
	info->screen_base = kmap->virtual;

	drm_fb_helper_fill_var(info, helper, fb->width, fb->height);	
	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	ret = 1;
out_err:
	if (ret < 0) {
		if (obj)
			fb->dev->driver->gem_free_object(obj);
		if (ptr)
			kfree(ptr);
	}
	return ret;
}

/** Sets the color ramps on behalf of fbcon */
static void
via_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
		u16 blue, int regno)
{
}

static void
via_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
		u16 *blue, int regno)
{
}

static struct drm_fb_helper_funcs via_fb_helper_funcs = {
	.gamma_set = via_fb_gamma_set,
	.gamma_get = via_fb_gamma_get,
	.fb_probe = via_fb_probe,
};

static struct fb_ops viafb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

int via_framebuffer_init(struct drm_device *dev, struct drm_fb_helper **ptr)
{
	struct drm_fb_helper *helper;
	struct fb_info *info;
	int ret = -ENOMEM;

	dev->mode_config.funcs = (void *)&via_mode_funcs;

	info = framebuffer_alloc(sizeof(*helper), dev->dev);
	if (!info)
		return ret;

	helper = info->par;
	helper->fbdev = info;
	helper->funcs = &via_fb_helper_funcs;

	strcpy(info->fix.id, "viadrmfb");
	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &viafb_ops; 

	info->pixmap.size = 64*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	/* Should be based on the crtc color map size */
	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto out_err;

	/* 2 CRTC and 2 Connectors ? */
	ret = drm_fb_helper_init(dev, helper, 1, 1);
	if (ret) {
		fb_dealloc_cmap(&info->cmap);
		goto out_err;
	}

	drm_fb_helper_single_add_all_connectors(helper);
	drm_fb_helper_initial_config(helper, 32);
	*ptr = helper;
out_err:
	if (ret)
		kfree(info);
	return ret;
}

void via_framebuffer_fini(struct drm_fb_helper *helper)
{
	struct ttm_bo_kmap_obj *kmap = helper->helper_private;
	struct fb_info *info = helper->fbdev;
	struct drm_gem_object *obj;

	drm_fb_helper_fini(helper);

	unregister_framebuffer(info);
	if (info->cmap.len)
		fb_dealloc_cmap(&info->cmap);

	if (kmap) {
		if (!ttm_bo_reserve(kmap->bo, true, false, false, 0))
			ttm_bo_kunmap(kmap);
		ttm_bo_unreserve(kmap->bo);
		ttm_bo_unref(&kmap->bo);
	}

	obj = helper->fb->helper_private;
	helper->fb->dev->driver->gem_free_object(obj);
	drm_framebuffer_cleanup(helper->fb);

	framebuffer_release(info);
}
