/*
 * Copyright © 2012 James Simmons
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * James Simmons <jsimmons@infradead.org>
 */
#include "via_drv.h"
#include "crtc_hw.h"


/*
 * Enables or disables analog (VGA) output.
 */
static void
via_analog_power(struct via_device *dev_priv, bool outputState)
{
	DRM_DEBUG_KMS("Entered via_analog_power.\n");

	via_analog_set_power(VGABASE, outputState);
	DRM_INFO("Analog (VGA) Power: %s\n",
			outputState ? "On" : "Off");

	DRM_DEBUG_KMS("Exiting via_analog_power.\n");
}

/*
 * Set analog (VGA) sync polarity.
 */
static void
via_analog_sync_polarity(struct via_device *dev_priv, unsigned int flags)
{
	u8 syncPolarity = 0x00;

	DRM_DEBUG_KMS("Entered via_analog_sync_polarity.\n");

	if (flags & DRM_MODE_FLAG_NHSYNC) {
		syncPolarity |= BIT(0);
	}

	if (flags & DRM_MODE_FLAG_NVSYNC) {
		syncPolarity |= BIT(1);
	}

	via_analog_set_sync_polarity(VGABASE, syncPolarity);
	DRM_INFO("Analog (VGA) Horizontal Sync Polarity: %s\n",
		(syncPolarity & BIT(0)) ? "-" : "+");
	DRM_INFO("Analog (VGA) Vertical Sync Polarity: %s\n",
		(syncPolarity & BIT(1)) ? "-" : "+");

	DRM_DEBUG_KMS("Exiting via_analog_sync_polarity.\n");
}

/*
 * Sets analog (VGA) display source.
 */
static void
via_analog_display_source(struct via_device *dev_priv, int index)
{
	u8 displaySource = index;

	DRM_DEBUG_KMS("Entered via_analog_display_source.\n");

	via_analog_set_display_source(VGABASE, displaySource & 0x01);
	DRM_INFO("Analog (VGA) Display Source: IGA%d\n",
			(displaySource & 0x01) + 1);

	DRM_DEBUG_KMS("Exiting via_analog_display_source.\n");
}

/*
 * Routines for controlling stuff on the analog port
 */
static const struct drm_encoder_funcs via_dac_enc_funcs = {
	.destroy = via_encoder_cleanup,
};

/*
 * Manage the power state of analog (VGA) DAC.
 */
static void
via_analog_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_device *dev_priv = encoder->dev->dev_private;

	DRM_DEBUG_KMS("Entered via_analog_dpms.\n");

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_ON);
		via_analog_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_STANDBY:
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_STANDBY);
		via_analog_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_SUSPEND:
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_SUSPEND);
		via_analog_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_OFF:
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_OFF);
		via_analog_power(dev_priv, false);
		break;
	default:
		DRM_ERROR("Bad DPMS mode.");
		break;
	}

	DRM_DEBUG_KMS("Exiting via_analog_dpms.\n");
}

/* Pass our mode to the connectors and the CRTC to give them a chance to
 * adjust it according to limitations or connector properties, and also
 * a chance to reject the mode entirely. Useful for things like scaling.
 */
static bool
via_analog_mode_fixup(struct drm_encoder *encoder,
		 const struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode)
{
	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

/*
 * Handle analog (VGA) mode setting.
 */
static void
via_analog_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct via_device *dev_priv = encoder->dev->dev_private;
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);

	DRM_DEBUG_KMS("Entered via_analog_mode_set.\n");

	via_analog_sync_polarity(dev_priv, adjusted_mode->flags);
	via_analog_display_source(dev_priv, iga->index);

	DRM_DEBUG_KMS("Exiting via_analog_mode_set.\n");
}

static void
via_analog_prepare(struct drm_encoder *encoder)
{
	struct via_device *dev_priv = encoder->dev->dev_private;

	DRM_DEBUG_KMS("Entered via_analog_prepare.\n");

	if (encoder->crtc) {
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_OFF);
		via_analog_power(dev_priv, false);
	}

	DRM_DEBUG_KMS("Exiting via_analog_prepare.\n");
}

static void
via_analog_commit(struct drm_encoder *encoder)
{
	struct via_device *dev_priv = encoder->dev->dev_private;

	DRM_DEBUG_KMS("Entered via_analog_commit.\n");

	if (encoder->crtc) {
		via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_ON);
		via_analog_power(dev_priv, true);
	}

	DRM_DEBUG_KMS("Exiting via_analog_commit.\n");
}

static void
via_analog_disable(struct drm_encoder *encoder)
{
	struct via_device *dev_priv = encoder->dev->dev_private;

	DRM_DEBUG_KMS("Entered via_analog_disable.\n");

	via_analog_set_dpms_control(VGABASE, VIA_ANALOG_DPMS_OFF);
	via_analog_power(dev_priv, false);

	DRM_DEBUG_KMS("Exiting via_analog_disable.\n");
}

static const struct drm_encoder_helper_funcs via_dac_enc_helper_funcs = {
	.dpms = via_analog_dpms,
	.mode_fixup = via_analog_mode_fixup,
	.mode_set = via_analog_mode_set,
	.prepare = via_analog_prepare,
	.commit = via_analog_commit,
	.disable = via_analog_disable,
};

static enum drm_connector_status
via_analog_detect(struct drm_connector *connector, bool force)
{
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	enum drm_connector_status ret = connector_status_disconnected;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				ret = connector_status_connected;
			}

			kfree(edid);
			goto exit;
		}
	}

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				ret = connector_status_connected;
			}

			kfree(edid);
			goto exit;
		}
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static const struct drm_connector_funcs via_analog_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_analog_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = via_connector_set_property,
	.destroy = via_connector_destroy,
};

static int via_analog_get_modes(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	int count = 0;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				drm_mode_connector_update_edid_property(connector,
									edid);
				count = drm_add_edid_modes(connector, edid);
			}

			kfree(edid);
			goto exit;
		}
	}

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				drm_mode_connector_update_edid_property(connector,
									edid);
				count = drm_add_edid_modes(connector, edid);
			}

			kfree(edid);
			goto exit;
		}
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return count;
}

static const struct drm_connector_helper_funcs via_analog_connector_helper_funcs = {
	.mode_valid = via_connector_mode_valid,
	.get_modes = via_analog_get_modes,
	.best_encoder = via_best_encoder,
};

void via_analog_probe(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	u16 chipset = dev->pdev->device;
	u8 sr13, sr5a;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch (chipset) {
	case PCI_DEVICE_ID_VIA_VT3157:
	case PCI_DEVICE_ID_VIA_VT1122:
	case PCI_DEVICE_ID_VIA_VX875:
	case PCI_DEVICE_ID_VIA_VX900_VGA:
		sr5a = vga_rseq(VGABASE, 0x5a);
		DRM_DEBUG_KMS("SR5A: 0x%02x\n", sr5a);

		/* Setting SR5A[0] to 1.
		 * This allows the reading out the alternative
		 * pin strapping information from SR12 and SR13. */
		svga_wseq_mask(VGABASE, 0x5a, BIT(0), BIT(0));
		DRM_DEBUG_KMS("SR5A: 0x%02x\n", sr5a);

		sr13 = vga_rseq(VGABASE, 0x13);
		DRM_DEBUG_KMS("SR13: 0x%02x\n", sr13);

		if (!(sr13 & BIT(2))) {
			dev_priv->analog_presence = true;
			DRM_DEBUG_KMS("Detected the presence of VGA.\n");
		} else {
			dev_priv->analog_presence = false;
		}

		/* Restore SR5A. */
		vga_wseq(VGABASE, 0x5a, sr5a);
		break;
	default:
		dev_priv->analog_presence = true;
		DRM_DEBUG_KMS("Detected the presence of VGA.\n");
		break;
	}

	dev_priv->analog_i2c_bus = VIA_I2C_NONE;

	if (dev_priv->analog_presence) {
		dev_priv->analog_i2c_bus = VIA_I2C_BUS2 | VIA_I2C_BUS1;
	}

	dev_priv->mapped_i2c_bus |= dev_priv->analog_i2c_bus;

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

void via_analog_init(struct drm_device *dev)
{
	struct via_connector *con;
	struct via_encoder *enc;
	struct via_device *dev_priv = dev->dev_private;

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate connector and encoder\n");
		return;
	}
	con = &enc->cons[0];
	INIT_LIST_HEAD(&con->props);

	/* Piece together our connector */
	drm_connector_init(dev, &con->base, &via_analog_connector_funcs,
				DRM_MODE_CONNECTOR_VGA);
	drm_connector_helper_add(&con->base, &via_analog_connector_helper_funcs);
	drm_connector_register(&con->base);

	con->i2c_bus = dev_priv->analog_i2c_bus;
	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = true;

	/* Setup the encoders and attach them */
	drm_encoder_init(dev, &enc->base, &via_dac_enc_funcs, DRM_MODE_ENCODER_DAC);
	drm_encoder_helper_add(&enc->base, &via_dac_enc_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;
	enc->di_port = VIA_DI_PORT_NONE;

	drm_mode_connector_attach_encoder(&con->base, &enc->base);
}
