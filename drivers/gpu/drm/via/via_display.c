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
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	James Simmons <jsimmons@infradead.org>
 */

#include "drmP.h"
#include "drm_mode.h"
#include "drm_crtc_helper.h"

#include "via_drv.h"
#include "via_disp_reg.h"

static int
via_iga1_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			uint32_t handle, uint32_t width, uint32_t height)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	int max_height = 64, max_width = 64, ret = 0;
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj = NULL;
	uint32_t temp;

	if (!handle) {
		/* turn off cursor */
		temp = VIA_READ(VIA_REG_HI_CONTROL1);
		VIA_WRITE(VIA_REG_HI_CONTROL1, temp & 0xFFFFFFFA);
		obj = iga->cursor_bo;
		goto unpin;
	}

	if (dev->pdev->device == PCI_DEVICE_ID_VIA_CLE266 ||
	    dev->pdev->device == PCI_DEVICE_ID_VIA_KM400) {
		max_height >>= 1;
		max_width >>= 1;
	}

	if ((height > max_height) || (width > max_width)) {
		DRM_ERROR("bad cursor width or height %d x %d\n", width, height);
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, crtc->base.id);
		return -ENOENT;
	}

	/* set_cursor, show_cursor */
	iga->cursor_bo = obj;
unpin:
	if (obj)
		drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int
via_iga2_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			uint32_t handle, uint32_t width, uint32_t height)
{
	return 0;
}

static int
via_iga1_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	return 0;
}

static int
via_iga2_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	return 0;
}

static void
via_iga1_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green, u16 *blue,
			uint32_t start, uint32_t size)
{
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	int end = (start + size > 256) ? 256 : start + size, i;
	u8 val;

	if (!crtc->enabled || !crtc->fb)
		return;

	if (crtc->fb->bits_per_pixel == 8) {
		u8 sr1a = vga_rseq(VGABASE, 0x1a);

		/* Prepare for initialize IGA1's LUT: */
		vga_wseq(VGABASE, 0x1a, sr1a & 0xfe);
		/* Change to Primary Display's LUT. */
		val = vga_rseq(VGABASE, 0x1b);
		vga_wseq(VGABASE, 0x1b, val);
		val = vga_rcrt(VGABASE, 0x67);
		vga_wcrt(VGABASE, 0x67, val);

		/* Fill in IGA1's LUT. */
		for (i = start; i < end; i++) {
			/* Bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xff);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		/* enable LUT */
		vga_wseq(VGABASE, 0x1b, BIT(0));
		/*  Disable gamma in case it was enabled previously */
		vga_wcrt(VGABASE, 0x33, BIT(7));	
		/* access Primary Display's LUT. */
		vga_wseq(VGABASE, 0x1a, sr1a & 0xfe);
	} else {
		val = vga_rseq(VGABASE, 0x1a);

		/* Enable Gamma */
		vga_wcrt(VGABASE, 0x80, BIT(7));
		vga_wseq(VGABASE, 0x00, BIT(0));

		/* Fill in IGA1's gamma. */
		for (i = start; i < end; i++) {
			/* bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xff);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		vga_wseq(VGABASE, 0x1a, val);
	}
}

static void
via_iga2_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
			u16 *blue, uint32_t start, uint32_t size)
{
}

static void
via_crtc_destroy(struct drm_crtc *crtc)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);

	drm_crtc_cleanup(crtc);

	if (iga->cursor_bo)
		crtc->dev->driver->gem_free_object(iga->cursor_bo);
}

static const struct drm_crtc_funcs via_iga1_funcs = {
	.cursor_set = via_iga1_cursor_set,
	.cursor_move = via_iga1_cursor_move,
	.gamma_set = via_iga1_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = via_crtc_destroy,
};

static const struct drm_crtc_funcs via_iga2_funcs = {
	.cursor_set = via_iga2_cursor_set,
	.cursor_move = via_iga2_cursor_move,
	.gamma_set = via_iga2_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = via_crtc_destroy,
};

void via_lock_crt(void __iomem *regs)
{
	u8 orig = (vga_rcrt(regs, 0x11) & ~0x80);

	vga_wcrt(regs, 0x11, (orig | 0x80));
}

void via_unlock_crt(void __iomem *regs)
{
	u8 orig = (vga_rcrt(regs, 0x11) & ~0x80);

	vga_wcrt(regs, 0x11, orig);
	orig = (vga_rcrt(regs, 0x47) & ~0x01);
	vga_wcrt(regs, 0x47, orig);
}

static void
enable_second_display_channel(struct drm_via_private *dev_priv)
{
	u8 orig = vga_rcrt(VGABASE, 0x6A) & ~BIT(6);

	vga_wcrt(VGABASE, 0x6A, orig);
	vga_wcrt(VGABASE, 0x6A, (orig | BIT(7)));
	orig |= (BIT(7) | BIT(6));
	vga_wcrt(VGABASE, 0x6A, orig);
	orig = ~0x3F;
	vga_wcrt(VGABASE, 0x6A, orig);
}

static void
disable_second_display_channel(struct drm_via_private *dev_priv)
{
	u8 orig = vga_rcrt(VGABASE, 0x6A) & ~BIT(6);

	vga_wcrt(VGABASE, 0x6A, orig);
	orig &= ~BIT(7);
	vga_wcrt(VGABASE, 0x6A, orig);
	vga_wcrt(VGABASE, 0x6A, (orig | BIT(6)));
}

static void 
via_load_FIFO_reg(struct via_crtc *iga, struct drm_display_mode *mode)
{
	struct drm_device *dev = iga->base.dev;
	int hor_active = mode->hdisplay, ver_active = mode->vdisplay;
	struct drm_via_private *dev_priv = dev->dev_private;
	int queue_expire_num, reg_value;

	/* If resolution > 1280x1024, expire length = 64, else
 	   expire length = 128 */
	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_K8M800 ||
	     dev->pdev->device == PCI_DEVICE_ID_VIA_CN700) &&
	    ((hor_active > 1280) && (ver_active > 1024)))
		queue_expire_num = 16;
	else
		queue_expire_num = iga->display_queue_expire_num;

        if (!iga->index) {
		/* Set Display FIFO Depth Select */
		reg_value = IGA1_FIFO_DEPTH_SELECT_FORMULA(iga->fifo_max_depth);
		load_value_to_registers(VGABASE, &iga->fifo_depth, reg_value);
        } else {
		/* Set Display FIFO Depth Select */
		reg_value = IGA2_FIFO_DEPTH_SELECT_FORMULA(iga->fifo_max_depth);
		if (dev->pdev->device == PCI_DEVICE_ID_VIA_K8M800) 
			reg_value--;
		load_value_to_registers(VGABASE, &iga->fifo_depth, reg_value);
	}

	/* Set Display FIFO Threshold Select */
	reg_value = iga->fifo_threshold / 4;
	load_value_to_registers(VGABASE, &iga->threshold, reg_value);

	/* Set FIFO High Threshold Select */
	reg_value = iga->fifo_high_threshold / 4;
	load_value_to_registers(VGABASE, &iga->high_threshold, reg_value);

	/* Set Display Queue Expire Num */
	reg_value = queue_expire_num / 4;
	load_value_to_registers(VGABASE, &iga->display_queue, reg_value);
}

void via_load_crtc_timing(struct via_crtc *iga, struct drm_display_mode *mode)
{
	struct drm_device *dev = iga->base.dev;
	struct drm_via_private *dev_priv = dev->dev_private;
	int reg_value = 0;

	via_unlock_crt(VGABASE);

	if (!iga->index) {
		reg_value = IGA1_HOR_TOTAL_FORMULA(mode->crtc_htotal);
		load_value_to_registers(VGABASE, &iga->timings.htotal, reg_value);

		reg_value = IGA1_HOR_ADDR_FORMULA(mode->crtc_hdisplay);
		load_value_to_registers(VGABASE, &iga->timings.hdisplay, reg_value);

		reg_value = IGA1_HOR_BLANK_START_FORMULA(mode->crtc_hblank_start);
		load_value_to_registers(VGABASE, &iga->timings.hblank_start, reg_value);

		reg_value = IGA1_HOR_BLANK_END_FORMULA(mode->crtc_hblank_end);
		load_value_to_registers(VGABASE, &iga->timings.hblank_end, reg_value);
	
		reg_value = IGA1_HOR_SYNC_START_FORMULA(mode->crtc_hsync_start);
		load_value_to_registers(VGABASE, &iga->timings.hsync_start, reg_value);

		reg_value = IGA1_HOR_SYNC_END_FORMULA(mode->crtc_hsync_end);
		load_value_to_registers(VGABASE, &iga->timings.hsync_end, reg_value);

		reg_value = IGA1_VER_TOTAL_FORMULA(mode->crtc_vtotal);
		load_value_to_registers(VGABASE, &iga->timings.vtotal, reg_value);

		reg_value = IGA1_VER_ADDR_FORMULA(mode->crtc_vdisplay);
		load_value_to_registers(VGABASE, &iga->timings.vdisplay, reg_value);

		reg_value = IGA1_VER_BLANK_START_FORMULA(mode->crtc_vblank_start);
		load_value_to_registers(VGABASE, &iga->timings.vblank_start, reg_value);

		reg_value = IGA1_VER_BLANK_END_FORMULA(mode->crtc_vblank_end);
		load_value_to_registers(VGABASE, &iga->timings.vblank_end, reg_value);

		reg_value = IGA1_VER_SYNC_START_FORMULA(mode->crtc_vsync_start);
		load_value_to_registers(VGABASE, &iga->timings.vsync_start, reg_value);

		reg_value = IGA1_VER_SYNC_END_FORMULA(mode->crtc_vsync_end);
		load_value_to_registers(VGABASE, &iga->timings.vsync_end, reg_value);
	} else {
		reg_value = IGA2_HOR_TOTAL_FORMULA(mode->crtc_htotal);
		load_value_to_registers(VGABASE, &iga->timings.htotal, reg_value);

		reg_value = IGA2_HOR_ADDR_FORMULA(mode->crtc_hdisplay);
		load_value_to_registers(VGABASE, &iga->timings.hdisplay, reg_value);

		reg_value = IGA2_HOR_BLANK_START_FORMULA(mode->crtc_hblank_start);
		load_value_to_registers(VGABASE, &iga->timings.hblank_start, reg_value);
	
		reg_value = IGA2_HOR_BLANK_END_FORMULA(mode->crtc_hblank_end);
		load_value_to_registers(VGABASE, &iga->timings.hblank_end, reg_value);

		reg_value = IGA2_HOR_SYNC_START_FORMULA(mode->crtc_hsync_start);
		load_value_to_registers(VGABASE, &iga->timings.hsync_start, reg_value);

		reg_value = IGA2_HOR_SYNC_END_FORMULA(mode->crtc_hsync_end);
		load_value_to_registers(VGABASE, &iga->timings.hsync_end, reg_value);

		reg_value = IGA2_VER_TOTAL_FORMULA(mode->crtc_vtotal);
		load_value_to_registers(VGABASE, &iga->timings.vtotal, reg_value);

		reg_value = IGA2_VER_ADDR_FORMULA(mode->crtc_vdisplay);
		load_value_to_registers(VGABASE, &iga->timings.vdisplay, reg_value);

		reg_value = IGA2_VER_BLANK_START_FORMULA(mode->crtc_vblank_start);
		load_value_to_registers(VGABASE, &iga->timings.vblank_start, reg_value);

		reg_value = IGA2_VER_BLANK_END_FORMULA(mode->crtc_vblank_end);
		load_value_to_registers(VGABASE, &iga->timings.vblank_end, reg_value);

		reg_value = IGA2_VER_SYNC_START_FORMULA(mode->crtc_vsync_start);
		load_value_to_registers(VGABASE, &iga->timings.vsync_start, reg_value);

		reg_value = IGA2_VER_SYNC_END_FORMULA(mode->crtc_vsync_end);
		load_value_to_registers(VGABASE, &iga->timings.vsync_end, reg_value);
	}
	via_lock_crt(VGABASE);
}

static void
via_iga1_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	u8 orig = (vga_rseq(VGABASE, 0x01) & ~BIT(5));

	switch (mode) {
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		/* turn off CRT screen (IGA1) */

		vga_wseq(VGABASE, 0x01, (BIT(5) | orig));
		drm_vblank_pre_modeset(crtc->dev, 0);
		break;

	case DRM_MODE_DPMS_ON:
		/* turn on CRT screen (IGA1) */

		drm_vblank_post_modeset(crtc->dev, 0);
		vga_wseq(VGABASE, 0x00, orig);
		break;
	}
}

static void
via_iga2_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	u8 orig = (vga_rseq(VGABASE, 0x6b) & ~BIT(2));

	switch (mode) {
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		/* turn off CRT screen (IGA2) */

		vga_wcrt(VGABASE, 0x6b, (BIT(2) | orig));
		disable_second_display_channel(dev_priv);
		drm_vblank_pre_modeset(crtc->dev, 1);
		break;
	case DRM_MODE_DPMS_ON:
		/* turn on CRT screen (IGA2) */

		drm_vblank_post_modeset(crtc->dev, 1);
		enable_second_display_channel(dev_priv);
		vga_wcrt(VGABASE, 0x6b, orig);
		break;
	}
}

static void
via_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs;

	/* Turn off the cursor */

	/* Blank the screen */
	if (crtc->enabled) {
		crtc_funcs = crtc->helper_private;
		crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
	}

	/* Once VQ is working it will have to be disabled here */
}

static void
via_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs;

	/* Turn on the cursor */

	/* Turn on the monitor */
	if (crtc->enabled) {
		crtc_funcs = crtc->helper_private;

		crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
	}
}

static bool
via_crtc_mode_fixup(struct drm_crtc *crtc, struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
via_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y, struct drm_framebuffer *old_fb)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	struct drm_device *dev = crtc->dev;
	int value;
	u8 orig;

	if (!fb)
		fb = old_fb;

	vga_r(VGABASE, VGA_IS1_RC);
	vga_w(VGABASE, VGA_ATT_IW, 0x00);

	/* Write Misc Register */
	vga_w(VGABASE, VGA_MIS_W, 0xC7);

	orig = (vga_rseq(VGABASE, 0x15) & ~0xA2);
	vga_wseq(VGABASE, 0x15, (orig | 0xA2));

	regs_init(VGABASE);

        if (!iga->index) {
		via_unlock_crt(VGABASE);
		vga_wcrt(VGABASE, 0x09, 0x00);	/*initial CR09=0 */
		orig = (vga_rcrt(VGABASE, 0x11) & ~0x70);	
		vga_wcrt(VGABASE, 0x11, orig);
		orig = (vga_rcrt(VGABASE, 0x17) & ~BIT(7));
		vga_wcrt(VGABASE, 0x17, orig);
        }

	/* Write CRTC */
	via_load_crtc_timing(iga, mode);

	/* always set to 1 */
	orig = (vga_rcrt(VGABASE, 0x03) & ~0x80);	
	vga_wcrt(VGABASE, 0x03, (orig | 0x80));	
	/* line compare should set all bits = 1 (extend modes) */
	vga_wcrt(VGABASE, 0x18, 0xFF);
	/* line compare should set all bits = 1 (extend modes) */
	orig = (vga_rcrt(VGABASE, 0x07) & ~0x10);
	vga_wcrt(VGABASE, 0x07, (orig | 0x10));
	/* line compare should set all bits = 1 (extend modes) */
	orig = (vga_rcrt(VGABASE, 0x09) & ~0x40);
	vga_wcrt(VGABASE, 0x09, (orig | 0x40));
	/* line compare should set all bits = 1 (extend modes) */
	orig = (vga_rcrt(VGABASE, 0x35) & ~0x10);
	vga_wcrt(VGABASE, 0x35, (orig | 0x10));
	/* line compare should set all bits = 1 (extend modes) */
	orig = (vga_rcrt(VGABASE, 0x33) & ~0x06);
	vga_wcrt(VGABASE, 0x33, (orig | 0x06));
	/* extend mode always set to e3h */
	vga_wcrt(VGABASE, 0x17, 0xE3);
	/* extend mode always set to 0h */
	vga_wcrt(VGABASE, 0x08, 0x00);
	/* extend mode always set to 0h */
	vga_wcrt(VGABASE, 0x14, 0x00);

	/* If K8M800, enable Prefetch Mode. */
	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_K8M800) || 
	    (dev->pdev->device == PCI_DEVICE_ID_VIA_K8M890)) {
		orig = (vga_rcrt(VGABASE, 0x33) & ~0x08);
		vga_wcrt(VGABASE, 0x33, (orig | 0x08));
	}

	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_CLE266) &&
	    (dev_priv->revision == CLE266_REVISION_AX)) {
		orig = (vga_rcrt(VGABASE, 0x1A) & ~0x02);
		vga_wcrt(VGABASE, 0x1A, (orig | 0x02));
	}

	via_lock_crt(VGABASE);
	orig = (vga_rcrt(VGABASE, 0x17) | BIT(7));
	vga_wcrt(VGABASE, 0x17, orig);

	/* Load Fetch registers */
        if (!iga->index)
		value = IGA1_FETCH_COUNT_FORMULA(mode->hdisplay,
						fb->bits_per_pixel / 8);
	else
		value = IGA2_FETCH_COUNT_FORMULA(mode->hdisplay,
						fb->bits_per_pixel / 8);
	load_value_to_registers(VGABASE, &iga->fetch, value);

	/* Load FIFO */
	if ((dev->pdev->device != PCI_DEVICE_ID_VIA_CLE266) &&
	    (dev->pdev->device != PCI_DEVICE_ID_VIA_KM400)) {
		via_load_FIFO_reg(iga, mode);
	} else if (adjusted_mode->hdisplay == 1024 &&
		   adjusted_mode->vdisplay == 768) { 
		/* Update Patch Register */
		orig = (vga_rseq(VGABASE, 0x16) & ~0xBF);
		vga_wseq(VGABASE, 0x16, (orig | 0x0C));
		vga_wseq(VGABASE, 0x18, 0x4C);
	}
	vga_r(VGABASE, VGA_IS1_RC);
	vga_w(VGABASE, VGA_ATT_IW, 0x20);

	via_set_pll(crtc, mode);

	return crtc_funcs->mode_set_base(crtc, crtc->x, crtc->y, old_fb);
}

static int
via_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	enum mode_set_atomic state = old_fb ? LEAVE_ATOMIC_MODE_SET : ENTER_ATOMIC_MODE_SET;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_framebuffer *new_fb = crtc->fb;
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct drm_gem_object *obj;
	int ret = 0, i;
	uint32_t *ptr;

	/* no fb bound */
	if (!new_fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return ret;
	}

	obj = new_fb->helper_private;
	placement = obj->filp->private_data;
	bo = obj->driver_private;

	ptr = (uint32_t *) placement->placement;
	for (i = 0; i < placement->num_placement; i++)
		*ptr++ |= TTM_PL_FLAG_NO_EVICT;
//	ret = ttm_bo_validate(bo, placement, false, false, false);
	if (likely(ret == 0)) {
		ret = crtc_funcs->mode_set_base_atomic(crtc, new_fb, x, y, state);
	}

	/* Free the framebuffer */
	ptr = (uint32_t *) placement->placement;
	for (i = 0; i < placement->num_placement; i++)
		*ptr++ &= ~TTM_PL_FLAG_NO_EVICT;
	/*if (ttm_bo_validate(bo, placement, false, false, false))
		DRM_ERROR("framebuffer still locked\n");*/
	return ret;
}

static int
via_iga1_mode_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
				int x, int y, enum mode_set_atomic state)
{
	u32 addr = y * fb->pitch + x * (fb->bits_per_pixel >> 3), pitch;
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	struct drm_gem_object *obj = fb->helper_private;
	struct ttm_buffer_object *bo = obj->driver_private;
	u8 value, orig;

	/*if ((state == ENTER_ATOMIC_MODE_SET) && (fb != crtc->fb))
		disable_accel(dev);
	else
		restore_accel(dev);*/

	/* Set the framebuffer offset */
	addr += bo->offset;

	vga_wcrt(VGABASE, 0x0D, addr & 0xFF);
	vga_wcrt(VGABASE, 0x0C, (addr >> 8) & 0xFF);
	vga_wcrt(VGABASE, 0x34, (addr >> 16) & 0xFF);
	orig = (vga_rcrt(VGABASE, 0x48) & ~0x1F);
	vga_wcrt(VGABASE, 0x48, ((addr >> 24) & 0x1F) | orig);

	/* Fetch register handling *
	pitch = ((fb->pitch * (fb->bits_per_pixel >> 3)) >> 4) + 4;
	vga_wseq(VGABASE, 0x1C, (pitch & 7));
	vga_wseq(VGABASE, 0x1D, ((pitch >> 3) & 0x03));*/

	if ((state == ENTER_ATOMIC_MODE_SET) || crtc->fb->pitch != fb->pitch) {
		/* Spec does not say that first adapter skips 3 bits but old
		 * code did it and seems to be reasonable in analogy to
		 * second adapter */
		pitch = fb->pitch >> 3;
		vga_wcrt(VGABASE, 0x13, pitch & 0xFF);
		orig = (vga_rcrt(VGABASE, 0x35) & ~0xE0);
		vga_wcrt(VGABASE, 0x35, ((pitch >> 3) & 0xE0) | orig);
	}

	if ((state == ENTER_ATOMIC_MODE_SET) || crtc->fb->depth != fb->depth) {
		switch (fb->depth) {
		case 8:
			value = 0x00;
			break;
		case 15:
			value = 0x04;
			break;
		case 16:
			value = 0x14;
			break;
		case 24:
			value = 0x0C;
			break;
		case 32:
			value = 0x08;
			break;
		default:
			DRM_ERROR("Unsupported depth: %d\n", fb->depth);
			return -EINVAL;
		}
		orig = (vga_rseq(VGABASE, 0x15) & ~0x1C);
		vga_wseq(VGABASE, 0x15, (value & 0x1C) | orig);
	}
	return 0;
}

static int
via_iga2_mode_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
				int x, int y, enum mode_set_atomic state)
{
	u32 addr = y * fb->pitch + x * (fb->bits_per_pixel >> 3), pitch;
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	struct drm_gem_object *obj = fb->helper_private;
	struct ttm_buffer_object *bo = obj->driver_private;
	u8 value, orig;

	/* Set the framebuffer offset */
	addr += bo->offset;

	/* Secondary display supports only quadword aligned memory */
	vga_wcrt(VGABASE, 0x62, (addr >> 2) & 0xfe);
	vga_wcrt(VGABASE, 0x63, (addr >> 10) & 0xff);
	vga_wcrt(VGABASE, 0x64, (addr >> 18) & 0xff);
	orig = (vga_rcrt(VGABASE, 0xA3) & ~0x07);
	vga_wcrt(VGABASE, 0xA3, ((addr >> 26) & 0x07) | orig);

	if ((state == ENTER_ATOMIC_MODE_SET) || crtc->fb->pitch != fb->pitch) {
		/* Set secondary pitch */
		pitch = fb->pitch >> 3;
		vga_wcrt(VGABASE, 0x66, pitch & 0xFF);
		orig = (vga_rcrt(VGABASE, 0x67) & ~0x03);
		vga_wcrt(VGABASE, 0x67, ((pitch >> 8) & 0x03) | orig);
		orig = (vga_rcrt(VGABASE, 0x71) & ~0x80);
		vga_wcrt(VGABASE, 0x71, ((pitch >> 3) & 0x80) | orig);
	}

	if ((state == ENTER_ATOMIC_MODE_SET) || crtc->fb->depth != fb->depth) {
		switch (fb->depth) {
		case 8:
			value = 0x00;
			break;
		case 16:
			value = 0x40;
			break;
		case 24:
			value = 0xC0;
			break;
		case 32:
			value = 0x80;
			break;
		default:
			DRM_ERROR("Unsupported depth: %d\n", fb->depth);
			return -EINVAL;
		}
		orig = (vga_rseq(VGABASE, 0x67) & ~0xC0);
		vga_wseq(VGABASE, 0x67, (value & 0xC0) | orig);
	}
	return 0;
}

static void 
drm_mode_crtc_load_lut(struct drm_crtc *crtc)
{
	int size = crtc->gamma_size * sizeof(uint16_t);
	void *r_base, *g_base, *b_base;

	if (size) {
		r_base = crtc->gamma_store;
		g_base = r_base + size;
		b_base = g_base + size;
		crtc->funcs->gamma_set(crtc, r_base, g_base, b_base,
					0, crtc->gamma_size);
	}
}

static const struct drm_crtc_helper_funcs via_iga1_helper_funcs = {
	.dpms = via_iga1_dpms,
	.prepare = via_crtc_prepare,
	.commit = via_crtc_commit,
	.mode_fixup = via_crtc_mode_fixup,
	.mode_set = via_crtc_mode_set,
	.mode_set_base = via_crtc_mode_set_base,
	.mode_set_base_atomic = via_iga1_mode_set_base_atomic,
	.load_lut = drm_mode_crtc_load_lut,
};

static const struct drm_crtc_helper_funcs via_iga2_helper_funcs = {
	.dpms = via_iga2_dpms,
	.prepare = via_crtc_prepare,
	.commit = via_crtc_commit,
	.mode_fixup = via_crtc_mode_fixup,
	.mode_set = via_crtc_mode_set,
	.mode_set_base = via_crtc_mode_set_base,
	.mode_set_base_atomic = via_iga2_mode_set_base_atomic,
	.load_lut = drm_mode_crtc_load_lut,
};

static void 
via_crtc_init(struct drm_device *dev, int index)
{
	struct drm_via_private *dev_priv = dev->dev_private;
	struct via_crtc *iga = &dev_priv->iga[index];
	struct drm_crtc *crtc = &iga->base;

	iga->index = index;
	if (index) {
		drm_crtc_init(dev, crtc, &via_iga2_funcs);
		drm_crtc_helper_add(crtc, &via_iga2_helper_funcs);

		/* Always start off IGA2 disabled until we detected something
		   attached to it */
		disable_second_display_channel(dev_priv);

		iga->timings.htotal.count = ARRAY_SIZE(iga2_hor_total);
		iga->timings.htotal.regs = iga2_hor_total;

		iga->timings.hdisplay.count = ARRAY_SIZE(iga2_hor_addr);
		iga->timings.hdisplay.regs = iga2_hor_addr;

		iga->timings.hblank_start.count = ARRAY_SIZE(iga2_hor_blank_start);
		iga->timings.hblank_start.regs = iga2_hor_blank_start;

		iga->timings.hblank_end.count = ARRAY_SIZE(iga2_hor_blank_end);
		iga->timings.hblank_end.regs = iga2_hor_blank_end;
	
		if (PCI_DEVICE_ID_VIA_CN700 <= dev->pdev->device)
			iga->timings.hsync_start.count = ARRAY_SIZE(iga2_hor_sync_start);
		else
			iga->timings.hsync_start.count = 3;
		iga->timings.hsync_start.regs = iga2_hor_sync_start;

		iga->timings.hsync_end.count = ARRAY_SIZE(iga2_hor_sync_end);
		iga->timings.hsync_end.regs = iga2_hor_sync_end;

		iga->timings.vtotal.count = ARRAY_SIZE(iga2_ver_total);
		iga->timings.vtotal.regs = iga2_ver_total;

		iga->timings.vdisplay.count = ARRAY_SIZE(iga2_ver_addr);
		iga->timings.vdisplay.regs = iga2_ver_addr;

		iga->timings.vblank_start.count = ARRAY_SIZE(iga2_ver_blank_start);
		iga->timings.vblank_start.regs = iga2_ver_blank_start;

		iga->timings.vblank_end.count = ARRAY_SIZE(iga2_ver_blank_end);
		iga->timings.vblank_end.regs = iga2_ver_blank_end;

		iga->timings.vsync_start.count = ARRAY_SIZE(iga2_ver_sync_start);
		iga->timings.vsync_start.regs = iga2_ver_sync_start;

		iga->timings.vsync_end.count = ARRAY_SIZE(iga2_ver_sync_end);
		iga->timings.vsync_end.regs = iga2_ver_sync_end;


		iga->high_threshold.count = ARRAY_SIZE(iga2_fifo_high_threshold_select);
		iga->high_threshold.regs = iga2_fifo_high_threshold_select;
		
		iga->threshold.count = ARRAY_SIZE(iga2_fifo_threshold_select);	
		iga->threshold.regs = iga2_fifo_threshold_select;

		iga->display_queue.count = ARRAY_SIZE(iga2_display_queue_expire_num);
		iga->display_queue.regs = iga2_display_queue_expire_num;
		
		iga->fifo_depth.count = ARRAY_SIZE(iga2_fifo_depth_select);
		iga->fifo_depth.regs = iga2_fifo_depth_select;

		iga->fetch.count = ARRAY_SIZE(iga2_fetch_count);
		iga->fetch.regs = iga2_fetch_count;

		switch (dev->pdev->device) {
		case PCI_DEVICE_ID_VIA_K8M800:
			iga->display_queue_expire_num = 0;
			iga->fifo_high_threshold = 296;
			iga->fifo_threshold = 328;
			iga->fifo_max_depth = 384;
			break;

		case PCI_DEVICE_ID_VIA_PM800:
			iga->display_queue_expire_num = 0;
			iga->fifo_high_threshold = 64;
			iga->fifo_threshold = 128;
			iga->fifo_max_depth = 192;
			break;

		case PCI_DEVICE_ID_VIA_CN700:
			iga->display_queue_expire_num = 0;
			iga->fifo_high_threshold = 64;
			iga->fifo_threshold = 80;
			iga->fifo_max_depth = 96;
			break;

		// CX700
		case PCI_DEVICE_ID_VIA_VT3157:
			iga->fifo_high_threshold = iga->fifo_threshold = 128;
			iga->display_queue_expire_num = 124;
			iga->fifo_max_depth = 192;
			break;

		// K8M890
		case PCI_DEVICE_ID_VIA_K8M890:
			iga->display_queue_expire_num = 124;
			iga->fifo_high_threshold = 296;
			iga->fifo_threshold = 328;
			iga->fifo_max_depth = 360;
			break;

		// P4M890
		case PCI_DEVICE_ID_VIA_VT3343:
			iga->display_queue_expire_num = 32;
			iga->fifo_high_threshold = 64;
			iga->fifo_threshold = 76;
			iga->fifo_max_depth = 96;
			break;

		// P4M900
		case PCI_DEVICE_ID_VIA_P4M900:
			iga->fifo_high_threshold = iga->fifo_threshold = 76;
			iga->display_queue_expire_num = 32;
			iga->fifo_max_depth = 96;
			break;

		// VX800
		case PCI_DEVICE_ID_VIA_VT1122:
			iga->fifo_high_threshold = iga->fifo_threshold = 152;
			iga->display_queue_expire_num = 64;
			iga->fifo_max_depth = 192;
			break;

		// VX855
		case PCI_DEVICE_ID_VIA_VX875:
		// VX900
		case PCI_DEVICE_ID_VIA_VX900:
			iga->fifo_high_threshold = iga->fifo_threshold = 320;
			iga->display_queue_expire_num = 160;
			iga->fifo_max_depth = 400;
		default:
			break;
		}

	} else {
		drm_crtc_init(dev, crtc, &via_iga1_funcs);
		drm_crtc_helper_add(crtc, &via_iga1_helper_funcs);

		iga->timings.htotal.count = ARRAY_SIZE(iga1_hor_total);
		iga->timings.htotal.regs = iga1_hor_total;

		iga->timings.hdisplay.count = ARRAY_SIZE(iga1_hor_addr);
		iga->timings.hdisplay.regs = iga1_hor_addr;

		iga->timings.hblank_start.count = ARRAY_SIZE(iga1_hor_blank_start);
		iga->timings.hblank_start.regs = iga1_hor_blank_start;

		iga->timings.hblank_end.count = ARRAY_SIZE(iga1_hor_blank_end);
		iga->timings.hblank_end.regs = iga1_hor_blank_end;
	
		iga->timings.hsync_start.count = ARRAY_SIZE(iga1_hor_sync_start);
		iga->timings.hsync_start.regs = iga1_hor_sync_start;

		iga->timings.hsync_end.count = ARRAY_SIZE(iga1_hor_sync_end);
		iga->timings.hsync_end.regs = iga1_hor_sync_end;

		iga->timings.vtotal.count = ARRAY_SIZE(iga1_ver_total);
		iga->timings.vtotal.regs = iga1_ver_total;

		iga->timings.vdisplay.count = ARRAY_SIZE(iga1_ver_addr);
		iga->timings.vdisplay.regs = iga1_ver_addr;

		iga->timings.vblank_start.count = ARRAY_SIZE(iga1_ver_blank_start);
		iga->timings.vblank_start.regs = iga1_ver_blank_start;

		iga->timings.vblank_end.count = ARRAY_SIZE(iga1_ver_blank_end);
		iga->timings.vblank_end.regs = iga1_ver_blank_end;

		iga->timings.vsync_start.count = ARRAY_SIZE(iga1_ver_sync_start);
		iga->timings.vsync_start.regs = iga1_ver_sync_start;

		iga->timings.vsync_end.count = ARRAY_SIZE(iga1_ver_sync_end);
		iga->timings.vsync_end.regs = iga1_ver_sync_end;


		iga->high_threshold.count = ARRAY_SIZE(iga1_fifo_high_threshold_select);
		iga->high_threshold.regs = iga1_fifo_high_threshold_select;

		iga->threshold.count = ARRAY_SIZE(iga1_fifo_threshold_select);	
		iga->threshold.regs = iga1_fifo_threshold_select;

		iga->display_queue.count = ARRAY_SIZE(iga1_display_queue_expire_num);
		iga->display_queue.regs = iga1_display_queue_expire_num;
		
		iga->fifo_depth.count = ARRAY_SIZE(iga1_fifo_depth_select);
		iga->fifo_depth.regs = iga1_fifo_depth_select;

		iga->fetch.count = ARRAY_SIZE(iga1_fetch_count);
		iga->fetch.regs = iga1_fetch_count;

		switch (dev->pdev->device) {
		case PCI_DEVICE_ID_VIA_K8M800:
			iga->display_queue_expire_num = 128;
			iga->fifo_high_threshold = 296;
			iga->fifo_threshold = 328;
			iga->fifo_max_depth = 384;
			break;

		case PCI_DEVICE_ID_VIA_PM800:
			iga->display_queue_expire_num = 128;
			iga->fifo_high_threshold = 32;
			iga->fifo_threshold = 64;
			iga->fifo_max_depth = 96;
			break;

		case PCI_DEVICE_ID_VIA_CN700:
			iga->display_queue_expire_num = 128;
			iga->fifo_high_threshold = 32;
			iga->fifo_threshold = 80;
			iga->fifo_max_depth = 96;
			break;

		// CX700
		case PCI_DEVICE_ID_VIA_VT3157:
			iga->display_queue_expire_num = 128;
			iga->fifo_high_threshold = 32;
			iga->fifo_threshold = 64;
			iga->fifo_max_depth = 96;
			break;

		// K8M890
		case PCI_DEVICE_ID_VIA_K8M890:
			iga->display_queue_expire_num = 124;
			iga->fifo_high_threshold = 296;
			iga->fifo_threshold = 328;
			iga->fifo_max_depth = 360;
			break;

		// P4M890
		case PCI_DEVICE_ID_VIA_VT3343:
			iga->display_queue_expire_num = 32;
			iga->fifo_high_threshold = 64;
			iga->fifo_threshold = 76;
			iga->fifo_max_depth = 96;
			break;

		// P4M900
		case PCI_DEVICE_ID_VIA_P4M900:
			iga->fifo_high_threshold = iga->fifo_threshold = 76;
			iga->display_queue_expire_num = 32;
			iga->fifo_max_depth = 96;
			break;

		// VX800
		case PCI_DEVICE_ID_VIA_VT1122:
			iga->display_queue_expire_num = 128;
			iga->fifo_high_threshold = 32;
			iga->fifo_threshold = 64;
			iga->fifo_max_depth = 96;
			break;

		// VX855
		case PCI_DEVICE_ID_VIA_VX875:
			iga->fifo_high_threshold = iga->fifo_threshold = 160;
			iga->display_queue_expire_num = 320;
			iga->fifo_max_depth = 200;
			break;

		// VX900
		case PCI_DEVICE_ID_VIA_VX900:
			iga->fifo_high_threshold = iga->fifo_threshold = 160;
			iga->display_queue_expire_num = 320;
			iga->fifo_max_depth = 192;
		default:
			break;
		}
	}
	drm_mode_crtc_set_gamma_size(crtc, 256);
}

int via_modeset_init(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = dev->dev_private;
	int i;

	drm_mode_config_init(dev);

	/* What is the max ? */
	dev->mode_config.min_width = 320;
	dev->mode_config.min_height = 200;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	via_i2c_init(dev);

	for (i = 0; i < 2; i++)
		via_crtc_init(dev, i);

	via_analog_init(dev);

	/*
	 * Set up the framebuffer device
	 */
	return via_framebuffer_init(dev, &dev_priv->helper);
}

void via_modeset_fini(struct drm_device *dev)
{
	struct drm_via_private *dev_priv = dev->dev_private;

	via_framebuffer_fini(dev_priv->helper);
	
	drm_mode_config_cleanup(dev);

	via_i2c_exit(dev);
}
