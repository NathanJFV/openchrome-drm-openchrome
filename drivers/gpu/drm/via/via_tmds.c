/*
 * Copyright © 2013 James Simmons
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
 *	James Simmons <jsimmons@infradead.org>
 */

#include "via_drv.h"

static void
viaTMDSPower(struct via_device *dev_priv,
                bool powerState)
{
    DRM_DEBUG("Entered viaTMDSPower.\n");

    if (powerState) {
        /* Software control for LVDS1 power sequence. */
        viaLVDS1SetPowerSeq(VGABASE, true);

        viaLVDS1SetSoftDisplayPeriod(VGABASE, true);
        viaLVDS1SetSoftData(VGABASE, true);
        viaTMDSSetPower(VGABASE, true);
    } else {
        /* Software control for LVDS1 power sequence. */
        viaLVDS1SetPowerSeq(VGABASE, true);

        viaTMDSSetPower(VGABASE, false);
        viaLVDS1SetSoftData(VGABASE, false);
        viaLVDS1SetSoftDisplayPeriod(VGABASE, false);
    }

    DRM_INFO("Integrated TMDS (DVI) Power: %s\n",
                powerState ? "On" : "Off");

    DRM_DEBUG("Exiting viaTMDSPower.\n");
}

/*
 * Routines for controlling stuff on the TMDS port
 */
static const struct drm_encoder_funcs via_tmds_enc_funcs = {
	.destroy = via_encoder_cleanup,
};

/* Manage the power state of the DAC */
static void
via_tmds_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_device *dev_priv = encoder->dev->dev_private;

    DRM_DEBUG("Entered via_tmds_dpms.\n");

	switch (mode) {
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
        viaTMDSPower(dev_priv, false);
		break;
	case DRM_MODE_DPMS_ON:
        viaTMDSPower(dev_priv, true);
		break;
	default:
        DRM_ERROR("Bad DPMS mode.");
	    break;
	}

    DRM_DEBUG("Exiting via_tmds_dpms.\n");
}

/* Pass our mode to the connectors and the CRTC to give them a chance to
 * adjust it according to limitations or connector properties, and also
 * a chance to reject the mode entirely. Usefule for things like scaling.
 */
static bool
via_tmds_mode_fixup(struct drm_encoder *encoder,
		 const struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode)
{
	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

static const struct drm_encoder_helper_funcs via_tmds_enc_helper_funcs = {
	.dpms = via_tmds_dpms,
	.mode_fixup = via_tmds_mode_fixup,
	.mode_set = via_set_sync_polarity,
	.prepare = via_encoder_prepare,
	.commit = via_encoder_commit,
	.disable = via_encoder_disable,
};

static enum drm_connector_status
via_dvi_detect(struct drm_connector *connector, bool force)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	enum drm_connector_status ret = connector_status_disconnected;
	struct edid *edid = NULL;

	drm_mode_connector_update_edid_property(connector, edid);
	if (con->ddc_bus) {
		edid = drm_get_edid(connector, con->ddc_bus);
		if (edid) {
			if ((connector->connector_type == DRM_MODE_CONNECTOR_DVIA) ^
			    (edid->input & DRM_EDID_INPUT_DIGITAL)) {
				drm_mode_connector_update_edid_property(connector, edid);
				ret = connector_status_connected;
			}
			kfree(edid);
		}
	}
	return ret;
}

static const struct drm_connector_funcs via_dvi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_dvi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = via_connector_set_property,
	.destroy = via_connector_destroy,
};

static const struct drm_connector_helper_funcs via_dvi_connector_helper_funcs = {
	.mode_valid = via_connector_mode_valid,
	.get_modes = via_get_edid_modes,
	.best_encoder = via_best_encoder,
};

int
via_tmds_init(struct drm_device *dev)
{
	struct via_device *dev_priv = dev->dev_private;
	struct via_connector *con;
	struct via_encoder *enc;
	int i2c_port = 0x31;

	if (!(vga_rseq(VGABASE, 0x3E) & BIT(5))) {
		DRM_INFO("Internal DVI not detected\n");
		return 1;
	}

	enc = kzalloc(sizeof(*enc) + 2 * sizeof(*con), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate connector and encoder\n");
		return -ENOMEM;
	}

	/* Setup the encoders and attach them */
	drm_encoder_init(dev, &enc->base, &via_tmds_enc_funcs, DRM_MODE_ENCODER_DAC);
	drm_encoder_helper_add(&enc->base, &via_tmds_enc_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;
	enc->diPort = DISP_DI_DFPL;

	/* Piece together our DVI-D connector */
	con = &enc->cons[0];
	drm_connector_init(dev, &con->base, &via_dvi_connector_funcs,
				DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(&con->base, &via_dvi_connector_helper_funcs);
	drm_connector_register(&con->base);

	con->ddc_bus = via_find_ddc_bus(i2c_port);
	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = true;
	INIT_LIST_HEAD(&con->props);

	drm_mode_connector_attach_encoder(&con->base, &enc->base);

	/* Now handle the DVI-A case */
	con = &enc->cons[1];
	drm_connector_init(dev, &con->base, &via_dvi_connector_funcs,
				DRM_MODE_CONNECTOR_DVIA);
	drm_connector_helper_add(&con->base, &via_dvi_connector_helper_funcs);
	drm_connector_register(&con->base);
	con->ddc_bus = via_find_ddc_bus(i2c_port);
	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = true;
	INIT_LIST_HEAD(&con->props);

	drm_mode_connector_attach_encoder(&con->base, &enc->base);
	return 0;
}
