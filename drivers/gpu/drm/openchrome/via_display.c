/*
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
 *
 * Authors:
 *	James Simmons <jsimmons@infradead.org>
 */

#include "drmP.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#include "via_drv.h"

/*
 * Shared encoder routines.
 */
void
via_encoder_commit(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct via_device *dev_priv = encoder->dev->dev_private;
	struct drm_device *dev = encoder->dev;
	struct via_crtc *iga = NULL;
	u8 value = 0;

	if (encoder->crtc == NULL)
		return;

	iga = container_of(encoder->crtc, struct via_crtc, base);
	if (iga->index)
		value = BIT(4);

	/* Set IGA source and turn on DI port clock */
	switch (enc->di_port) {
	case VIA_DI_PORT_DVP0:
		/* DVP0 Data Source Selection. */
		svga_wcrt_mask(VGABASE, 0x96, value, BIT(4));
		/* enable DVP0 under CX700 */
		if (encoder->dev->pdev->device == PCI_DEVICE_ID_VIA_VT3157)
			svga_wcrt_mask(VGABASE, 0x91, BIT(5), BIT(5));
		/* Turn on DVP0 clk */
		svga_wseq_mask(VGABASE, 0x1E, 0xC0, BIT(7) | BIT(6));
		break;

	case VIA_DI_PORT_DVP1:
		svga_wcrt_mask(VGABASE, 0x9B, value, BIT(4));
		/* enable DVP1 under these chipset. Does DVI exist
		 * for pre CX700 hardware */
		if ((dev->pdev->device == PCI_DEVICE_ID_VIA_VT3157) ||
		    (dev->pdev->device == PCI_DEVICE_ID_VIA_VT1122) ||
		    (dev->pdev->device == PCI_DEVICE_ID_VIA_VX875) ||
		    (dev->pdev->device == PCI_DEVICE_ID_VIA_VX900_VGA))
			svga_wcrt_mask(VGABASE, 0xD3, 0x00, BIT(5));
		/* Turn on DVP1 clk */
		svga_wseq_mask(VGABASE, 0x1E, 0x30, BIT(5) | BIT(4));
		break;

	case VIA_DI_PORT_DFPH:
		/* Port 96 is used on older hardware for the DVP0 */
		if ((dev->pdev->device != PCI_DEVICE_ID_VIA_VT3157) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VT1122) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VX875) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900_VGA))
			svga_wcrt_mask(VGABASE, 0x96, value, BIT(4));

		svga_wcrt_mask(VGABASE, 0x97, value, BIT(4));
		/* Turn on DFPH clock */
		svga_wseq_mask(VGABASE, 0x2A, 0x0C, BIT(3) | BIT(2));
		break;

	case VIA_DI_PORT_DFPL:
		/* Port 9B is used on older hardware for the DVP1 */
		if ((dev->pdev->device != PCI_DEVICE_ID_VIA_VT3157) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VT1122) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VX875) &&
		    (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900_VGA))
			svga_wcrt_mask(VGABASE, 0x9B, value, BIT(4));

		svga_wcrt_mask(VGABASE, 0x99, value, BIT(4));
		/* Turn on DFPL clock */
		svga_wseq_mask(VGABASE, 0x2A, 0x03, BIT(1) | BIT(0));
		break;

	case VIA_DI_PORT_DFP:
		if ((dev->pdev->device == PCI_DEVICE_ID_VIA_K8M890) ||
		    (dev->pdev->device == PCI_DEVICE_ID_VIA_VT3343))
			svga_wcrt_mask(VGABASE, 0x97, 0x84,
					BIT(7) | BIT(2) | BIT(1) | BIT(0));

		svga_wcrt_mask(VGABASE, 0x97, value, BIT(4));
		svga_wcrt_mask(VGABASE, 0x99, value, BIT(4));
		/* Turn on DFP clk */
		svga_wseq_mask(VGABASE, 0x2A, 0x0F, BIT(3) | BIT(2) | BIT(1) | BIT(0));
		break;

	/* For TTL Type LCD */
	case (VIA_DI_PORT_DFPL + VIA_DI_PORT_DVP1):
		svga_wcrt_mask(VGABASE, 0x99, value, BIT(4));
		svga_wcrt_mask(VGABASE, 0x9B, value, BIT(4));

		/* Turn on DFPL, DVP1 clk */
		svga_wseq_mask(VGABASE, 0x2A, 0x03, BIT(1) | BIT(0));
		svga_wseq_mask(VGABASE, 0x1E, 0x30, BIT(5) | BIT(4));
		break;

	/* For 409 TTL Type LCD */
	case (VIA_DI_PORT_DFPH + VIA_DI_PORT_DFPL + VIA_DI_PORT_DVP1):
		svga_wcrt_mask(VGABASE, 0x97, value, BIT(4));
		svga_wcrt_mask(VGABASE, 0x99, value, BIT(4));
		svga_wcrt_mask(VGABASE, 0x9B, value, BIT(4));

		/* Turn on DFPHL, DVP1 clk */
		svga_wseq_mask(VGABASE, 0x2A, 0x0F, BIT(3) | BIT(2) | BIT(1) | BIT(0));
		svga_wseq_mask(VGABASE, 0x1E, 0x30, BIT(5) | BIT(4));
		break;

	default:
		DRM_ERROR("Unsupported DIPort.\n");
	case VIA_DI_PORT_NONE:
		break;
	}

	/* Older chipsets only used CR91 to control all DI ports.
	 * For newer chipsets (CX700 and above) CR91 and CRD3 are
	 * used to control DVP0 and DVP1 seperately */
	if (iga->index && dev->pdev->device != PCI_DEVICE_ID_VIA_VT3157)
		svga_wcrt_mask(VGABASE, 0x91, 0x00, BIT(5));

	/* Now turn on the display */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
}

void
via_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* First turn off the display */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);

	switch (enc->di_port) {
	case VIA_DI_PORT_DVP0:
		svga_wseq_mask(VGABASE, 0x1E, 0x00, BIT(7) | BIT(6));
		break;

	case VIA_DI_PORT_DVP1:
		svga_wseq_mask(VGABASE, 0x1E, 0x00, BIT(5) | BIT(4));
		break;

	case VIA_DI_PORT_DFPH:
		svga_wseq_mask(VGABASE, 0x2A, 0x00, BIT(3) | BIT(2));
		break;

	case VIA_DI_PORT_DFPL:
		svga_wseq_mask(VGABASE, 0x2A, 0x00, BIT(1) | BIT(0));
		break;

	case VIA_DI_PORT_DFP:
		svga_wseq_mask(VGABASE, 0x2A, 0x00,
				BIT(3) | BIT(2) | BIT(1) | BIT(0));
		break;

	/* TTL LCD, Quanta case */
	case VIA_DI_PORT_DFPL + VIA_DI_PORT_DVP1:
		svga_wseq_mask(VGABASE, 0x1E, 0x00, BIT(5) | BIT(4));
		svga_wseq_mask(VGABASE, 0x2A, 0x00, BIT(1) | BIT(0));
		break;

	case VIA_DI_PORT_DFPH + VIA_DI_PORT_DFPL + VIA_DI_PORT_DVP1:
		svga_wseq_mask(VGABASE, 0x1E, 0x00, BIT(5) | BIT(4));
		svga_wseq_mask(VGABASE, 0x2A, 0x00,
				BIT(3) | BIT(2) | BIT(1) | BIT(0));
		break;

	default:
		DRM_ERROR("Unsupported DIPort.\n");
	case VIA_DI_PORT_NONE:
		break;
	}
}

void
via_set_sync_polarity(struct drm_encoder *encoder, struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct via_device *dev_priv = encoder->dev->dev_private;
	u8 syncreg = 0;

	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		syncreg |= BIT(6);
	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		syncreg |= BIT(5);

	switch (enc->di_port) {
	case VIA_DI_PORT_DVP0:
		svga_wcrt_mask(VGABASE, 0x96, syncreg, BIT(6) | BIT(5));
		break;

	case VIA_DI_PORT_DVP1:
		svga_wcrt_mask(VGABASE, 0x9B, syncreg, BIT(6) | BIT(5));
		break;

	case VIA_DI_PORT_DFPH:
		svga_wcrt_mask(VGABASE, 0x97, syncreg, BIT(6) | BIT(5));
		break;

	case VIA_DI_PORT_DFPL:
		svga_wcrt_mask(VGABASE, 0x99, syncreg, BIT(6) | BIT(5));
		break;

	/* For TTL Type LCD */
	case (VIA_DI_PORT_DFPL + VIA_DI_PORT_DVP1):
		svga_wcrt_mask(VGABASE, 0x99, syncreg, BIT(6) | BIT(5));
		svga_wcrt_mask(VGABASE, 0x9B, syncreg, BIT(6) | BIT(5));
		break;

	default:
		DRM_ERROR("No DIPort.\n");
	case VIA_DI_PORT_NONE:
		break;
	}
}

void
via_encoder_prepare(struct drm_encoder *encoder)
{
	struct drm_encoder_helper_funcs *encoder_funcs = encoder->helper_private;

	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

struct drm_encoder *
via_best_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_encoder *encoder = NULL;
	struct drm_mode_object *obj;

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id,
						DRM_MODE_OBJECT_ENCODER);
		if (obj)
			encoder = obj_to_encoder(obj);
	}
	return encoder;
}

void via_encoder_cleanup(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);

	drm_encoder_cleanup(encoder);
	kfree(enc);
}

/*
 * Shared connector routines.
 */
int
via_get_edid_modes(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct edid *edid = drm_get_edid(&con->base, con->ddc_bus);

	return drm_add_edid_modes(connector, edid);
}

int
via_connector_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	/* Check Clock Range */
	if (mode->clock > 400000)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

int
via_connector_set_property(struct drm_connector *connector,
				struct drm_property *property, uint64_t value)
{
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_helper_funcs *encoder_funcs;
	struct drm_device *dev = connector->dev;

	if (encoder) {
		encoder_funcs = encoder->helper_private;

		if (property == dev->mode_config.dpms_property)
			encoder_funcs->dpms(encoder, (uint32_t)(value & 0xf));
	}
	return 0;
}

void
via_connector_destroy(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct drm_property *property, *tmp;

	list_for_each_entry_safe(property, tmp, &con->props, head)
		drm_property_destroy(connector->dev, property);
	list_del(&con->props);

	drm_mode_connector_update_edid_property(connector, NULL);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

/* Power sequence relations */
struct td_timer {
	struct vga_regset tdRegs[2];
};

static struct td_timer td_timer_regs[] = {
	/* td_timer0 */
	{ { { VGA_CRT_IC, 0x8B, 0, 7 }, { VGA_CRT_IC, 0x8F, 0, 3 } } },
	/* td_timer1 */
	{ { { VGA_CRT_IC, 0x8C, 0, 7 }, { VGA_CRT_IC, 0x8F, 4, 7 } } },
	/* td_timer2 */
	{ { { VGA_CRT_IC, 0x8D, 0, 7 }, { VGA_CRT_IC, 0x90, 0, 3 } } },
	/* td_timer3 */
	{ { { VGA_CRT_IC, 0x8E, 0, 7 }, { VGA_CRT_IC, 0x90, 4, 7 } } }
};

/*
 * Function Name:  via_init_td_timing_regs
 * Description: Init TD timing register (power sequence)
 */
static void
via_init_td_timing_regs(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	unsigned int td_timer[4] = { 500, 50, 0, 510 }, i;
	struct vga_registers timings;
	u32 reg_value;

	/* Fill primary power sequence */
	for (i = 0; i < 4; i++) {
		/* Calculate TD Timer, every step is 572.1uSec */
		reg_value = td_timer[i] * 10000 / 5721;

		timings.count = ARRAY_SIZE(td_timer_regs[i].tdRegs);
		timings.regs = td_timer_regs[i].tdRegs;
		load_value_to_registers(VGABASE, &timings, reg_value);
	}

	/* Note: VT3353 have two hardware power sequences
	 * other chips only have one hardware power sequence */
	if (dev->pdev->device == PCI_DEVICE_ID_VIA_VT1122) {
		/* set CRD4[0] to "1" to select 2nd LCD power sequence. */
		svga_wcrt_mask(VGABASE, 0xD4, BIT(0), BIT(0));
		/* Fill secondary power sequence */
		for (i = 0; i < 4; i++) {
			/* Calculate TD Timer, every step is 572.1uSec */
			reg_value = td_timer[i] * 10000 / 5721;

			timings.count = ARRAY_SIZE(td_timer_regs[i].tdRegs);
			timings.regs = td_timer_regs[i].tdRegs;
			load_value_to_registers(VGABASE, &timings, reg_value);
		}
	}
}

static void
via_i2c_reg_init(struct via_device *dev_priv)
{
	vga_wseq(VGABASE, 0x31, 0x01);
	svga_wseq_mask(VGABASE, 0x31, 0x30, 0x30);
	vga_wseq(VGABASE, 0x26, 0x01);
	svga_wseq_mask(VGABASE, 0x26, 0x30, 0x30);
	vga_wseq(VGABASE, 0x2C, 0xc2);
	vga_wseq(VGABASE, 0x3D, 0xc0);
	svga_wseq_mask(VGABASE, 0x2C, 0x30, 0x30);
	svga_wseq_mask(VGABASE, 0x3D, 0x30, 0x30);
}

static void
via_hwcursor_init(struct via_device *dev_priv)
{
	/* set 0 as transparent color key */
	VIA_WRITE(PRIM_HI_TRANSCOLOR, 0);
	VIA_WRITE(PRIM_HI_FIFO, 0x0D000D0F);
	VIA_WRITE(PRIM_HI_INVTCOLOR, 0X00FFFFFF);
	VIA_WRITE(V327_HI_INVTCOLOR, 0X00FFFFFF);

	/* set 0 as transparent color key */
	VIA_WRITE(HI_TRANSPARENT_COLOR, 0);
	VIA_WRITE(HI_INVTCOLOR, 0X00FFFFFF);
	VIA_WRITE(ALPHA_V3_PREFIFO_CONTROL, 0xE0000);
	VIA_WRITE(ALPHA_V3_FIFO_CONTROL, 0xE0F0000);

	/* Turn both cursors off. */
	VIA_WRITE(PRIM_HI_CTRL, VIA_READ(PRIM_HI_CTRL) & 0xFFFFFFFA);
	VIA_WRITE(HI_CONTROL, VIA_READ(HI_CONTROL) & 0xFFFFFFFA);
}

static void
via_init_crtc_regs(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;

	via_unlock_crtc(VGABASE, dev->pdev->device);

	/* always set to 1 */
	svga_wcrt_mask(VGABASE, 0x03, BIT(7), BIT(7));
	/* bits 0 to 7 of line compare */
	vga_wcrt(VGABASE, 0x18, 0xFF);
	/* bit 8 of line compare */
	svga_wcrt_mask(VGABASE, 0x07, BIT(4), BIT(4));
	/* bit 9 of line compare */
	svga_wcrt_mask(VGABASE, 0x09, BIT(6), BIT(6));
	/* bit 10 of line compare */
	svga_wcrt_mask(VGABASE, 0x35, BIT(4), BIT(4));
	/* adjust hsync by one character - value 011 */
	svga_wcrt_mask(VGABASE, 0x33, 0x06, 0x07);
	/* extend mode always set to e3h */
	vga_wcrt(VGABASE, 0x17, 0xE3);
	/* extend mode always set to 0h */
	vga_wcrt(VGABASE, 0x08, 0x00);
	/* extend mode always set to 0h */
	vga_wcrt(VGABASE, 0x14, 0x00);

	/* If K8M800, enable Prefetch Mode. */
	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_K8M800) ||
	    (dev->pdev->device == PCI_DEVICE_ID_VIA_K8M890))
		svga_wcrt_mask(VGABASE, 0x33, 0x00, BIT(3));

	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_CLE266) &&
	    (dev_priv->revision == CLE266_REVISION_AX))
		svga_wseq_mask(VGABASE, 0x1A, BIT(1), BIT(1));

	via_lock_crtc(VGABASE);
}

static void
via_display_init(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	u8 index = 0x3D, value;

	/* Check if spread spectrum is enabled */
	if (dev->pdev->device == PCI_DEVICE_ID_VIA_VX900_VGA)
		index = 0x2C;

	value = vga_rseq(VGABASE, 0x1E);
	if (value & BIT(3)) {
		value = vga_rseq(VGABASE, index);
		vga_wseq(VGABASE, index, value & 0xFE);

		value = vga_rseq(VGABASE, 0x1E);
		vga_wseq(VGABASE, 0x1E, value & 0xF7);

		dev_priv->spread_spectrum = true;
	} else
		dev_priv->spread_spectrum = false;

	/* Load fixed CRTC timing registers */
	via_init_crtc_regs(dev);

	/* Init TD timing register (power sequence) */
	via_init_td_timing_regs(dev);

	/* I/O address bit to be 1. Enable access */
	/* to frame buffer at A0000-BFFFFh */
	svga_wmisc_mask(VGABASE, BIT(0), BIT(0));
}

int
via_modeset_init(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	int ret = 0;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 320;
	dev->mode_config.min_height = 200;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	via_display_init(dev);
	via_i2c_reg_init(dev_priv);
	via_i2c_init(dev);
	via_hwcursor_init(dev_priv);

	for (i = 0; i < 2; i++)
		via_crtc_init(dev, i);

	via_analog_init(dev);

	via_lvds_init(dev);

	switch (dev->pdev->device) {
    /* Only CX700 / VX700 and VX800 have an integrated TMDS (DVI) 
     * transmitter. */
	case PCI_DEVICE_ID_VIA_VT3157:
	case PCI_DEVICE_ID_VIA_VT1122:
		via_tmds_init(dev);
		break;
    default:
        break;
    }

	switch (dev->pdev->device) {
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		via_hdmi_init(dev, VIA_DI_PORT_NONE);
		break;
	default:
		break;
	}

	/* Set up the framebuffer device */
	ret = via_fbdev_init(dev, &dev_priv->helper);
	if (!ret) {
		goto exit;
	}

	drm_kms_helper_poll_init(dev);
exit:
	return ret;
}

void via_modeset_fini(struct drm_device *dev)
{
	struct drm_connector *connector, *ot;
	struct drm_encoder *encoder, *enct;

	drm_kms_helper_poll_fini(dev);
	via_framebuffer_fini(dev);

	/* drm_mode_config_init has not been called yet */
	if (!dev->mode_config.dpms_property)
		return;

	/* We need to cleanup the connectors before the encoders */
	list_for_each_entry_safe(connector, ot,
				&dev->mode_config.connector_list, head)
		connector->funcs->destroy(connector);

	list_for_each_entry_safe(encoder, enct,
				&dev->mode_config.encoder_list, head)
		encoder->funcs->destroy(encoder);

	drm_mode_config_cleanup(dev);

	via_i2c_exit();
}
