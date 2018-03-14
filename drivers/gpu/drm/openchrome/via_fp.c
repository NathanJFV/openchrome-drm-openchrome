/*
 * Copyright 2017 Kevin Brace. All Rights Reserved.
 * Copyright 2012 James Simmons <jsimmons@infradead.org>. All Rights Reserved.
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
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
#include <linux/dmi.h>
#include <asm/olpc.h>

#include "via_drv.h"

#define TD0 200
#define TD1 25
#define TD2 0
#define TD3 25

/* Non-I2C bus FP native screen resolution information table.*/
static via_fp_info via_fp_info_table[] = {
	{ 640,  480},
	{ 800,  600},
	{1024,  768},
	{1280,  768},
	{1280, 1024},
	{1400, 1050},
	{1600, 1200},
	{1280,  800},
	{ 800,  480},
	{1024,  768},
	{1366,  768},
	{1024,  768},
	{1280,  768},
	{1280, 1024},
	{1400, 1050},
	{1600, 1200}
};

static bool openchrome_fp_probe_edid(struct i2c_adapter *i2c_bus)
{
	u8 out = 0x0;
	u8 buf[8];
	struct i2c_msg msgs[] = {
		{
			.addr = DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &out,
		},
		{
			.addr = DDC_ADDR,
			.flags = I2C_M_RD,
			.len = 8,
			.buf = buf,
		}
	};
	int i2c_ret;
	bool ret = false;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	i2c_ret = i2c_transfer(i2c_bus, msgs, 2);
	if (i2c_ret != 2) {
		goto exit;
	}

	if (drm_edid_header_is_valid(buf) < 6) {
		goto exit;
	}

	ret = true;
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

/* caculate the cetering timing using mode and adjusted_mode */
static void
via_centering_timing(const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	int panel_hsync_time = adjusted_mode->hsync_end -
		adjusted_mode->hsync_start;
	int panel_vsync_time = adjusted_mode->vsync_end -
		adjusted_mode->vsync_start;
	int panel_hblank_start = adjusted_mode->hdisplay;
	int panel_vbank_start = adjusted_mode->vdisplay;
	int hborder = (adjusted_mode->hdisplay - mode->hdisplay) / 2;
	int vborder = (adjusted_mode->vdisplay - mode->vdisplay) / 2;
	int new_hblank_start = hborder + mode->hdisplay;
	int new_vblank_start = vborder + mode->vdisplay;

	adjusted_mode->hdisplay = mode->hdisplay;
	adjusted_mode->hsync_start = (adjusted_mode->hsync_start -
		panel_hblank_start) + new_hblank_start;
	adjusted_mode->hsync_end = adjusted_mode->hsync_start +
		panel_hsync_time;
	adjusted_mode->vdisplay = mode->vdisplay;
	adjusted_mode->vsync_start = (adjusted_mode->vsync_start -
		panel_vbank_start) + new_vblank_start;
	adjusted_mode->vsync_end = adjusted_mode->vsync_start +
		panel_vsync_time;
	/* Adjust Crtc H and V */
	adjusted_mode->crtc_hdisplay = adjusted_mode->hdisplay;
	adjusted_mode->crtc_hblank_start = new_hblank_start;
	adjusted_mode->crtc_hblank_end = adjusted_mode->crtc_htotal - hborder;
	adjusted_mode->crtc_hsync_start = adjusted_mode->hsync_start;
	adjusted_mode->crtc_hsync_end = adjusted_mode->hsync_end;
	adjusted_mode->crtc_vdisplay = adjusted_mode->vdisplay;
	adjusted_mode->crtc_vblank_start = new_vblank_start;
	adjusted_mode->crtc_vblank_end = adjusted_mode->crtc_vtotal - vborder;
	adjusted_mode->crtc_vsync_start = adjusted_mode->vsync_start;
	adjusted_mode->crtc_vsync_end = adjusted_mode->vsync_end;
}

static void
via_fp_castle_rock_soft_power_seq(struct via_device *dev_priv,
					bool power_state)
{
	DRM_DEBUG_KMS("Entered via_fp_castle_rock_soft_power_seq.\n");

	if (power_state) {
		/* Wait for 25 ms. */
		mdelay(25);

		/* Turn on FP VDD rail. */
		via_fp_set_primary_soft_vdd(VGABASE, true);

		/* Wait for 510 ms. */
		mdelay(510);

		/* Turn on FP data transmission. */
		via_fp_set_primary_soft_data(VGABASE, true);

		/* Wait for 1 ms. */
		mdelay(1);

		/* Turn on FP VEE rail. */
		via_fp_set_primary_soft_vee(VGABASE, true);

		/* Turn on FP back light. */
		via_fp_set_primary_soft_back_light(VGABASE, true);
	} else {
		/* Wait for 1 ms. */
		mdelay(1);

		/* Turn off FP back light. */
		via_fp_set_primary_soft_back_light(VGABASE, false);

		/* Turn off FP VEE rail. */
		via_fp_set_primary_soft_vee(VGABASE, false);

		/* Wait for 510 ms. */
		mdelay(510);

		/* Turn off FP data transmission. */
		via_fp_set_primary_soft_data(VGABASE, false);

		/* Wait for 25 ms. */
		mdelay(25);

		/* Turn off FP VDD rail. */
		via_fp_set_primary_soft_vdd(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting via_fp_castle_rock_soft_power_seq.\n");
}

static void
via_fp_primary_soft_power_seq(struct via_device *dev_priv, bool power_state)
{
	DRM_DEBUG_KMS("Entered via_fp_primary_soft_power_seq.\n");

	/* Turn off FP hardware power sequence. */
	via_fp_set_primary_hard_power(VGABASE, false);

	/* Use software FP power sequence control. */
	via_fp_set_primary_power_seq_type(VGABASE, false);

	if (power_state) {
		/* Turn on FP display period. */
		via_fp_set_primary_direct_display_period(VGABASE, true);

		/* Wait for TD0 ms. */
		mdelay(TD0);

		/* Turn on FP VDD rail. */
		via_fp_set_primary_soft_vdd(VGABASE, true);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn on FP data transmission. */
		via_fp_set_primary_soft_data(VGABASE, true);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn on FP VEE rail. */
		via_fp_set_primary_soft_vee(VGABASE, true);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn on FP back light. */
		via_fp_set_primary_soft_back_light(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_fp_set_primary_soft_back_light(VGABASE, false);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn off FP VEE rail. */
		via_fp_set_primary_soft_vee(VGABASE, false);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn off FP data transmission. */
		via_fp_set_primary_soft_data(VGABASE, false);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn off FP VDD rail. */
		via_fp_set_primary_soft_vdd(VGABASE, false);

		/* Turn off FP display period. */
		via_fp_set_primary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting via_fp_primary_soft_power_seq.\n");
}

static void
via_fp_secondary_soft_power_seq(struct via_device *dev_priv, bool power_state)
{
	DRM_DEBUG_KMS("Entered via_fp_secondary_soft_power_seq.\n");

	/* Turn off FP hardware power sequence. */
	via_fp_set_secondary_hard_power(VGABASE, false);

	/* Use software FP power sequence control. */
	via_fp_set_secondary_power_seq_type(VGABASE, false);

	if (power_state) {
		/* Turn on FP display period. */
		via_fp_set_secondary_direct_display_period(VGABASE, true);

		/* Wait for TD0 ms. */
		mdelay(TD0);

		/* Turn on FP VDD rail. */
		via_fp_set_secondary_soft_vdd(VGABASE, true);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn on FP data transmission. */
		via_fp_set_secondary_soft_data(VGABASE, true);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn on FP VEE rail. */
		via_fp_set_secondary_soft_vee(VGABASE, true);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn on FP back light. */
		via_fp_set_secondary_soft_back_light(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_fp_set_secondary_soft_back_light(VGABASE, false);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn off FP VEE rail. */
		via_fp_set_secondary_soft_vee(VGABASE, false);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn off FP data transmission. */
		via_fp_set_secondary_soft_data(VGABASE, false);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn off FP VDD rail. */
		via_fp_set_secondary_soft_vdd(VGABASE, false);

		/* Turn off FP display period. */
		via_fp_set_secondary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting via_fp_secondary_soft_power_seq.\n");
}

static void
via_fp_primary_hard_power_seq(struct via_device *dev_priv, bool power_state)
{
	DRM_DEBUG_KMS("Entered via_fp_primary_hard_power_seq.\n");

	/* Use hardware FP power sequence control. */
	via_fp_set_primary_power_seq_type(VGABASE, true);

	if (power_state) {
		/* Turn on FP display period. */
		via_fp_set_primary_direct_display_period(VGABASE, true);

		/* Turn on FP hardware power sequence. */
		via_fp_set_primary_hard_power(VGABASE, true);

		/* Turn on FP back light. */
		via_fp_set_primary_direct_back_light_ctrl(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_fp_set_primary_direct_back_light_ctrl(VGABASE, false);

		/* Turn off FP hardware power sequence. */
		via_fp_set_primary_hard_power(VGABASE, false);

		/* Turn on FP display period. */
		via_fp_set_primary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Entered via_fp_primary_hard_power_seq.\n");
}

static void
via_fp_power(struct via_device *dev_priv, unsigned short device,
		u32 di_port, bool power_state)
{
	DRM_DEBUG_KMS("Entered via_fp_power.\n");

	switch (device) {
	case PCI_DEVICE_ID_VIA_CLE266:
		via_fp_castle_rock_soft_power_seq(dev_priv,
							power_state);
		break;
	case PCI_DEVICE_ID_VIA_KM400:
	case PCI_DEVICE_ID_VIA_CN700:
	case PCI_DEVICE_ID_VIA_PM800:
	case PCI_DEVICE_ID_VIA_K8M800:
	case PCI_DEVICE_ID_VIA_VT3343:
	case PCI_DEVICE_ID_VIA_K8M890:
	case PCI_DEVICE_ID_VIA_P4M900:
		via_fp_primary_hard_power_seq(dev_priv, power_state);
		break;
	case PCI_DEVICE_ID_VIA_VT3157:
	case PCI_DEVICE_ID_VIA_VT1122:
		if (di_port & VIA_DI_PORT_LVDS1) {
			via_fp_primary_soft_power_seq(dev_priv, power_state);
			via_lvds1_set_power(VGABASE, power_state);
		}

		if (di_port & VIA_DI_PORT_LVDS2) {
			via_fp_secondary_soft_power_seq(dev_priv, power_state);
			via_lvds2_set_power(VGABASE, power_state);
		}

		break;
	case PCI_DEVICE_ID_VIA_VX875:
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		via_fp_primary_hard_power_seq(dev_priv, power_state);
		via_lvds1_set_power(VGABASE, power_state);
		break;
	default:
		DRM_DEBUG_KMS("VIA Technologies Chrome IGP "
				"FP Power: Unrecognized "
				"PCI Device ID.\n");
		break;
	}

	DRM_DEBUG_KMS("Exiting via_fp_power.\n");
}

/*
 * Sets flat panel I/O pad state.
 */
static void via_fp_io_pad_setting(struct via_device *dev_priv, u32 di_port, bool io_pad_on)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPLOW:
		via_fpdp_low_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPHIGH:
		via_fpdp_high_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_FPDPLOW |
	      VIA_DI_PORT_FPDPHIGH):
		via_fpdp_low_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		via_fpdp_high_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_LVDS1 |
	      VIA_DI_PORT_LVDS2):
		via_lvds1_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		via_lvds2_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("FP I/O Pad: %s\n", io_pad_on ? "On": "Off");

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_fp_format(struct via_device *dev_priv,
				u32 di_port, u8 format)
{
	u8 temp = format & 0x01;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_format(VGABASE, temp);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_format(VGABASE, temp);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_format(VGABASE, temp);
		via_lvds2_set_format(VGABASE, temp);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_fp_output_format(struct via_device *dev_priv,
					u32 di_port, u8 output_format)
{
	u8 temp = output_format & 0x01;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_output_format(VGABASE, temp);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_output_format(VGABASE, temp);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_output_format(VGABASE, temp);
		via_lvds2_set_output_format(VGABASE, temp);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_fp_dithering(struct via_device *dev_priv,
				u32 di_port, bool dithering)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_dithering(VGABASE, dithering);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_dithering(VGABASE, dithering);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_dithering(VGABASE, dithering);
		via_lvds2_set_dithering(VGABASE, dithering);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

/*
 * Sets flat panel display source.
 */
static void
via_fp_display_source(struct via_device *dev_priv, u32 di_port, int index)
{
	u8 display_source = index & 0x01;

	DRM_DEBUG_KMS("Entered via_fp_display_source.\n");

	switch(di_port) {
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_FPDPLOW:
		via_fpdp_low_set_display_source(VGABASE, display_source);
		via_dvp1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_FPDPHIGH:
		via_fpdp_high_set_display_source(VGABASE, display_source);
		via_dvp0_set_display_source(VGABASE, display_source);
		break;
	case (VIA_DI_PORT_FPDPLOW |
		VIA_DI_PORT_FPDPHIGH):
		via_fpdp_low_set_display_source(VGABASE, display_source);
		via_fpdp_high_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_display_source(VGABASE, display_source);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_display_source(VGABASE, display_source);
		via_lvds2_set_display_source(VGABASE, display_source);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("FP Display Source: IGA%d\n",
			display_source + 1);

	DRM_DEBUG_KMS("Exiting via_fp_display_source.\n");
}

static void via_fp_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* PCI Device ID */
	u16 chipset = dev->pdev->device;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_fp_power(dev_priv, chipset, enc->di_port, true);
		via_fp_io_pad_setting(dev_priv, enc->di_port, true);
		break;
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		via_fp_power(dev_priv, chipset, enc->di_port, false);
		via_fp_io_pad_setting(dev_priv, enc->di_port, false);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static bool
via_lvds_mode_fixup(struct drm_encoder *encoder,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct drm_property *prop = encoder->dev->mode_config.scaling_mode_property;
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);
	struct drm_display_mode *native_mode = NULL, *tmp, *t;
	struct drm_connector *connector = NULL, *con;
	u64 scale_mode = DRM_MODE_SCALE_CENTER;
	struct drm_device *dev = encoder->dev;

	list_for_each_entry(con, &dev->mode_config.connector_list, head) {
		if (encoder ==  con->encoder) {
			connector = con;
			break;
		}
	}

	if (!connector) {
		DRM_INFO("LVDS encoder is not used by any connector\n");
		return false;
	}

	list_for_each_entry_safe(tmp, t, &connector->modes, head) {
		if (tmp->type & (DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER)) {
			native_mode = tmp;
			break;
		}
	}

	if (!native_mode) {
		DRM_INFO("No native mode for LVDS\n");
		return false;
	}

	drm_object_property_get_value(&connector->base, prop, &scale_mode);

	if ((mode->hdisplay != native_mode->hdisplay) ||
	    (mode->vdisplay != native_mode->vdisplay)) {
		if (scale_mode == DRM_MODE_SCALE_NONE)
			return false;
		drm_mode_copy(adjusted_mode, native_mode);
	}
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	iga->scaling_mode = VIA_NO_SCALING;
	/* Take care of 410 downscaling */
	if ((mode->hdisplay > native_mode->hdisplay) ||
	    (mode->vdisplay > native_mode->vdisplay)) {
		iga->scaling_mode = VIA_SHRINK;
	} else {
		if (!iga->index || scale_mode == DRM_MODE_SCALE_CENTER) {
			/* Do centering according to mode and adjusted_mode */
			via_centering_timing(mode, adjusted_mode);
		} else {
			if (mode->hdisplay < native_mode->hdisplay)
				iga->scaling_mode |= VIA_HOR_EXPAND;
			if (mode->vdisplay < native_mode->vdisplay)
				iga->scaling_mode |= VIA_VER_EXPAND;
		}
	}
	return true;
}

static void via_fp_prepare(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* PCI Device ID */
	u16 chipset = dev->pdev->device;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_fp_power(dev_priv, chipset, enc->di_port, false);
	via_fp_io_pad_setting(dev_priv, enc->di_port, false);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_fp_commit(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* PCI Device ID */
	u16 chipset = dev->pdev->device;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_fp_power(dev_priv, chipset, enc->di_port, true);
	via_fp_io_pad_setting(dev_priv, enc->di_port, true);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void
via_fp_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* PCI Device ID */
	u16 chipset = encoder->dev->pdev->device;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* Temporary implementation.*/
	switch (chipset) {
	case PCI_DEVICE_ID_VIA_P4M900:
		via_fpdp_low_set_adjustment(VGABASE, 0x08);
		break;
	default:
		break;
	}

 	switch (chipset) {
	case PCI_DEVICE_ID_VIA_VT3157:
	case PCI_DEVICE_ID_VIA_VT1122:
	case PCI_DEVICE_ID_VIA_VX875:
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		/* OPENLDI Mode */
		via_fp_format(dev_priv, enc->di_port, 0x01);

		/* Sequential Mode */
		via_fp_output_format(dev_priv, enc->di_port, 0x01);

		/* Turn on dithering. */
		via_fp_dithering(dev_priv, enc->di_port, true);
		break;
	default:
		break;
	}

	via_fp_display_source(dev_priv, enc->di_port, iga->index);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_fp_disable(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_device *dev_priv = encoder->dev->dev_private;

	/* PCI Device ID */
	u16 chipset = dev->pdev->device;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_fp_power(dev_priv, chipset, enc->di_port, false);
	via_fp_io_pad_setting(dev_priv, enc->di_port, false);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

const struct drm_encoder_helper_funcs via_lvds_helper_funcs = {
	.dpms = via_fp_dpms,
	.mode_fixup = via_lvds_mode_fixup,
	.prepare = via_fp_prepare,
	.commit = via_fp_commit,
	.mode_set = via_fp_mode_set,
	.disable = via_fp_disable,
};

const struct drm_encoder_funcs via_lvds_enc_funcs = {
	.destroy = via_encoder_cleanup,
};

/* Detect FP presence. */
static enum drm_connector_status
via_fp_detect(struct drm_connector *connector,  bool force)
{
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	struct via_device *dev_priv = connector->dev->dev_private;
	enum drm_connector_status ret = connector_status_disconnected;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;
	u8 mask;
	uint32_t i, i2c_bus_bit;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (machine_is_olpc()) {
		ret = connector_status_connected;
		goto exit;
	}

	i2c_bus_bit = VIA_I2C_BUS2;
	for (i = 0; i < 2; i++) {
		if (con->i2c_bus & i2c_bus_bit) {
			if (i2c_bus_bit & VIA_I2C_BUS2) {
				i2c_bus = via_find_ddc_bus(0x31);
			} else if (i2c_bus_bit & VIA_I2C_BUS3) {
				i2c_bus = via_find_ddc_bus(0x2c);
			} else {
				i2c_bus = NULL;
				i2c_bus_bit = i2c_bus_bit << 1;
				continue;
			}
		} else {
			i2c_bus = NULL;
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		if (!openchrome_fp_probe_edid(i2c_bus)) {
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL) {
				ret = connector_status_connected;
				kfree(edid);
				DRM_DEBUG_KMS("FP detected.\n");
				DRM_DEBUG_KMS("i2c_bus_bit: %x\n", i2c_bus_bit);
				goto exit;
			} else {
				kfree(edid);
			}
		}

		i2c_bus_bit = i2c_bus_bit << 1;
	}

	if (connector->dev->pdev->device ==
					PCI_DEVICE_ID_VIA_CLE266) {
		mask = BIT(3);
	} else {
		mask = BIT(1);
	}

	if (vga_rcrt(VGABASE, 0x3B) & mask) {
		ret = connector_status_connected;
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static int via_fp_set_property(struct drm_connector *connector,
				struct drm_property *property,
				uint64_t val)
{
	struct drm_device *dev = connector->dev;
	uint64_t orig;
	int ret;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	ret = drm_object_property_get_value(&connector->base, property, &orig);
	if (ret) {
		DRM_ERROR("FP Property not found!\n");
		ret = -EINVAL;
		goto exit;
	}

	if (orig != val) {
		if (property == dev->mode_config.scaling_mode_property) {
			switch (val) {
			case DRM_MODE_SCALE_NONE:
				break;
			case DRM_MODE_SCALE_CENTER:
				break;
			case DRM_MODE_SCALE_ASPECT:
				break;
			case DRM_MODE_SCALE_FULLSCREEN:
				break;
			default:
				DRM_ERROR("Invalid FP property!\n");
				ret = -EINVAL;
				break;
			}
		}
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

struct drm_connector_funcs via_fp_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_fp_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = via_fp_set_property,
	.destroy = via_connector_destroy,
};

static int
via_fp_get_modes(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct drm_device *dev = connector->dev;
	struct via_device *dev_priv = dev->dev_private;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;
	struct drm_display_mode *native_mode = NULL;
	u8 reg_value;
	int hdisplay, vdisplay;
	int count = 0;
	uint32_t i, i2c_bus_bit;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* OLPC is very special */
	if (machine_is_olpc()) {
		native_mode = drm_mode_create(dev);

		native_mode->clock = 56519;
		native_mode->hdisplay = 1200;
		native_mode->hsync_start = 1211;
		native_mode->hsync_end = 1243;
		native_mode->htotal = 1264;
		native_mode->hskew = 0;
		native_mode->vdisplay = 900;
		native_mode->vsync_start = 901;
		native_mode->vsync_end = 911;
		native_mode->vtotal = 912;
		native_mode->vscan = 0;
		native_mode->vrefresh = 50;
		native_mode->hsync = 0;

		native_mode->type = DRM_MODE_TYPE_PREFERRED |
					DRM_MODE_TYPE_DRIVER;
		drm_mode_set_name(native_mode);
		drm_mode_probed_add(connector, native_mode);
		count = 1;
		goto exit;
	}

	i2c_bus_bit = VIA_I2C_BUS2;
	for (i = 0; i < 2; i++) {
		if (con->i2c_bus & i2c_bus_bit) {
			if (i2c_bus_bit & VIA_I2C_BUS2) {
				i2c_bus = via_find_ddc_bus(0x31);
			} else if (i2c_bus_bit & VIA_I2C_BUS3) {
				i2c_bus = via_find_ddc_bus(0x2c);
			} else {
				i2c_bus = NULL;
				i2c_bus_bit = i2c_bus_bit << 1;
				continue;
			}
		} else {
			i2c_bus = NULL;
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL) {
				drm_mode_connector_update_edid_property(connector, edid);
				count = drm_add_edid_modes(connector, edid);
				kfree(edid);
				DRM_DEBUG_KMS("FP EDID information was obtained.\n");
				DRM_DEBUG_KMS("i2c_bus_bit: %x\n", i2c_bus_bit);
				break;
			} else {
				kfree(edid);
			}
		}

		i2c_bus_bit = i2c_bus_bit << 1;
	}

	reg_value = (vga_rcrt(VGABASE, 0x3f) & 0x0f);
	hdisplay = vdisplay = 0;
	hdisplay = via_fp_info_table[reg_value].x;
	vdisplay = via_fp_info_table[reg_value].y;

	if (hdisplay && vdisplay) {
		native_mode = drm_cvt_mode(dev, hdisplay, vdisplay,
					60, false, false, false);
	}

	if (native_mode) {
		native_mode->type = DRM_MODE_TYPE_PREFERRED |
						DRM_MODE_TYPE_DRIVER;
		drm_mode_set_name(native_mode);
		drm_mode_probed_add(connector, native_mode);
		count = 1;
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return count;
}

static int
via_fp_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	struct drm_property *prop = connector->dev->mode_config.scaling_mode_property;
	struct drm_display_mode *native_mode = NULL, *tmp, *t;
	struct drm_device *dev = connector->dev;
	u64 scale_mode = DRM_MODE_SCALE_CENTER;

	list_for_each_entry_safe(tmp, t, &connector->modes, head) {
		if (tmp->type & (DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER)) {
			native_mode = tmp;
			break;
		}
	}

	drm_object_property_get_value(&connector->base, prop, &scale_mode);

	if ((scale_mode == DRM_MODE_SCALE_NONE) &&
	    ((mode->hdisplay != native_mode->hdisplay) ||
	     (mode->vdisplay != native_mode->vdisplay)))
		return MODE_PANEL;

	/* Don't support mode larger than physical size */
	if (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900_VGA) {
		if (mode->hdisplay > native_mode->hdisplay)
			return MODE_PANEL;
		if (mode->vdisplay > native_mode->vdisplay)
			return MODE_PANEL;
	} else {
		/* HW limitation 410 only can
		 * do <= 1.33 scaling */
		if (mode->hdisplay * 100 > native_mode->hdisplay * 133)
			return MODE_PANEL;
		if (mode->vdisplay * 100 > native_mode->vdisplay * 133)
			return MODE_PANEL;

		/* Now we can not support H V different scale */
		if ((mode->hdisplay > native_mode->hdisplay) &&
		    (mode->vdisplay < native_mode->vdisplay))
			return MODE_PANEL;

		if ((mode->hdisplay < native_mode->hdisplay) &&
		    (mode->vdisplay > native_mode->vdisplay))
			return MODE_PANEL;
	}
	return MODE_OK;
}

struct drm_connector_helper_funcs via_fp_connector_helper_funcs = {
	.get_modes = via_fp_get_modes,
	.mode_valid = via_fp_mode_valid,
	.best_encoder = via_best_encoder,
};

/*
 * Probe (pre-initialization detection) FP.
 */
void via_fp_probe(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	struct drm_connector connector;
	struct i2c_adapter *i2c_bus;
	struct edid *edid;
	u16 chipset = dev->pdev->device;
	u8 sr12, sr13, sr5a;
	u8 cr3b;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	sr12 = vga_rseq(VGABASE, 0x12);
	sr13 = vga_rseq(VGABASE, 0x13);
	cr3b = vga_rcrt(VGABASE, 0x3b);

	DRM_DEBUG_KMS("chipset: 0x%04x\n", chipset);
	DRM_DEBUG_KMS("sr12: 0x%02x\n", sr12);
	DRM_DEBUG_KMS("sr13: 0x%02x\n", sr13);
	DRM_DEBUG_KMS("cr3b: 0x%02x\n", cr3b);

	/* Detect the presence of FPs. */
	switch (chipset) {
	case PCI_DEVICE_ID_VIA_CLE266:
		if ((sr12 & BIT(4)) || (cr3b & BIT(3))) {
			dev_priv->int_fp1_presence = true;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_DIP0;
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_KM400:
	case PCI_DEVICE_ID_VIA_CN700:
	case PCI_DEVICE_ID_VIA_PM800:
	case PCI_DEVICE_ID_VIA_K8M800:
		/* 3C5.13[3] - DVP0D8 pin strapping
		 *             0: AGP pins are used for AGP
		 *             1: AGP pins are used by FPDP
		 *             (Flat Panel Display Port) */
		if ((sr13 & BIT(3)) && (cr3b & BIT(1))) {
			dev_priv->int_fp1_presence = true;
			dev_priv->int_fp1_di_port =
						VIA_DI_PORT_FPDPHIGH |
						VIA_DI_PORT_FPDPLOW;
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_VT3343:
	case PCI_DEVICE_ID_VIA_K8M890:
	case PCI_DEVICE_ID_VIA_P4M900:
		if (cr3b & BIT(1)) {
			/* 3C5.12[4] - DVP0D4 pin strapping
			 *             0: 12-bit FPDP (Flat Panel
			 *                Display Port)
			 *             1: 24-bit FPDP (Flat Panel
			 *                Display Port) */
			if (sr12 & BIT(4)) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_FPDPLOW |
						VIA_DI_PORT_FPDPHIGH;
			} else {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_FPDPLOW;
			}
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_VT3157:
	case PCI_DEVICE_ID_VIA_VT1122:
	case PCI_DEVICE_ID_VIA_VX875:
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		/* Save SR5A. */
		sr5a = vga_rseq(VGABASE, 0x5a);

		DRM_DEBUG_KMS("sr5a: 0x%02x\n", sr5a);

		/* Set SR5A[0] to 1.
		 * This allows the read out of the alternative
		 * pin strapping settings from SR12 and SR13. */
		svga_wseq_mask(VGABASE, 0x5a, BIT(0), BIT(0));

		sr13 = vga_rseq(VGABASE, 0x13);
		if (cr3b & BIT(1)) {
			if (dev_priv->is_via_nanobook) {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = true;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_LVDS2;
			} else if (dev_priv->is_quanta_il1) {
				/* From the Quanta IL1 schematic. */
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_DVP1;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_NONE;

			/* 3C5.13[7:6] - Integrated LVDS / DVI
			 *               Mode Select (DVP1D15-14 pin
			 *               strapping)
			 *               00: LVDS1 + LVDS2
			 *               01: DVI + LVDS2
			 *               10: Dual LVDS Channel
			 *                   (High Resolution Panel)
			 *               11: One DVI only (decrease
			 *                   the clock jitter) */
			} else if ((!(sr13 & BIT(7))) &&
					(!(sr13 & BIT(6)))) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_LVDS1;

				/*
				 * For now, don't support the second
				 * FP.
				 */
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_NONE;
			} else if ((!(sr13 & BIT(7))) &&
					(sr13 & BIT(6))) {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = true;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_LVDS2;
			} else if ((sr13 & BIT(7)) &&
					(!(sr13 & BIT(6)))) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_LVDS1 |
						VIA_DI_PORT_LVDS2;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_NONE;
			} else {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port =
						VIA_DI_PORT_NONE;
			}
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
			dev_priv->int_fp2_presence = false;
			dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		}

		/* Restore SR5A. */
		vga_wseq(VGABASE, 0x5a, sr5a);
		break;
	default:
		dev_priv->int_fp1_presence = false;
		dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	}

	dev_priv->int_fp1_i2c_bus = VIA_I2C_NONE;
	dev_priv->int_fp2_i2c_bus = VIA_I2C_NONE;

	/* Zero clear connector struct.
	 * Not doing so leads to a crash. */
	memset(&connector, 0, sizeof(connector));

	/* Register a connector only for I2C bus probing. */
	drm_connector_init(dev, &connector, &via_fp_connector_funcs,
				DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&connector,
					&via_fp_connector_helper_funcs);
	drm_connector_register(&connector);

	if ((dev_priv->int_fp1_presence)
		&& (!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS2))) {
		i2c_bus = via_find_ddc_bus(0x31);
		edid = drm_get_edid(&connector, i2c_bus);
		if (edid) {
			dev_priv->int_fp1_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			kfree(edid);
		}
	}

	if ((dev_priv->int_fp2_presence)
		&& (!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS2))) {
		i2c_bus = via_find_ddc_bus(0x31);
		edid = drm_get_edid(&connector, i2c_bus);
		if (edid) {
			dev_priv->int_fp2_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			kfree(edid);
		}
	}

	/* Release the connector resource. */
	drm_connector_unregister(&connector);
	drm_connector_cleanup(&connector);

	DRM_DEBUG_KMS("int_fp1_presence: %x\n",
			dev_priv->int_fp1_presence);
	DRM_DEBUG_KMS("int_fp1_di_port: 0x%08x\n",
			dev_priv->int_fp1_di_port);
	DRM_DEBUG_KMS("int_fp1_i2c_bus: 0x%08x\n",
			dev_priv->int_fp1_i2c_bus);
	DRM_DEBUG_KMS("int_fp2_presence: %x\n",
			dev_priv->int_fp2_presence);
	DRM_DEBUG_KMS("int_fp2_di_port: 0x%08x\n",
			dev_priv->int_fp2_di_port);
	DRM_DEBUG_KMS("int_fp2_i2c_bus: 0x%08x\n",
			dev_priv->int_fp2_i2c_bus);
	DRM_DEBUG_KMS("mapped_i2c_bus: 0x%08x\n",
			dev_priv->mapped_i2c_bus);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

void via_fp_init(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	struct via_connector *con;
	struct via_encoder *enc;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if ((!(dev_priv->int_fp1_presence)) &&
		(!(dev_priv->int_fp2_presence))) {
		goto exit;
	}

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate FP.\n");
		goto exit;
	}

	con = &enc->cons[0];
	INIT_LIST_HEAD(&con->props);

	drm_connector_init(dev, &con->base, &via_fp_connector_funcs,
				DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&con->base, &via_fp_connector_helper_funcs);
	drm_connector_register(&con->base);

	if (dev_priv->int_fp1_presence) {
		con->i2c_bus = dev_priv->int_fp1_i2c_bus;
	} else if (dev_priv->int_fp2_presence) {
		con->i2c_bus = dev_priv->int_fp2_i2c_bus;
	} else {
		con->i2c_bus = VIA_I2C_NONE;
	}

	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = false;

	drm_mode_create_scaling_mode_property(dev);
	drm_object_attach_property(&con->base.base,
					dev->mode_config.scaling_mode_property,
					DRM_MODE_SCALE_CENTER);

	/* Now setup the encoder */
	drm_encoder_init(dev, &enc->base, &via_lvds_enc_funcs,
				DRM_MODE_ENCODER_LVDS);
	drm_encoder_helper_add(&enc->base, &via_lvds_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);

	if (dev_priv->int_fp1_presence) {
		enc->di_port = dev_priv->int_fp1_di_port;
	} else if (dev_priv->int_fp2_presence) {
		enc->di_port = dev_priv->int_fp2_di_port;
	} else {
		enc->di_port = VIA_DI_PORT_NONE;
	}

	/* Put it all together */
	drm_mode_connector_attach_encoder(&con->base, &enc->base);
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return;
}
