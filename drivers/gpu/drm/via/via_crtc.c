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
#include "drm_mode.h"
#include "drm_crtc_helper.h"

#include "via_drv.h"
#include "via_disp_reg.h"

static inline void
enable_second_display_channel(struct drm_via_private *dev_priv)
{
	svga_wcrt_mask(VGABASE, 0x6A, 0x00, BIT(6));
	svga_wcrt_mask(VGABASE, 0x6A, BIT(7), BIT(7));
	svga_wcrt_mask(VGABASE, 0x6A, BIT(6), BIT(6));
}

static inline void
disable_second_display_channel(struct drm_via_private *dev_priv)
{
	svga_wcrt_mask(VGABASE, 0x6A, 0x00, BIT(6));
	svga_wcrt_mask(VGABASE, 0x6A, 0x00, BIT(7));
	svga_wcrt_mask(VGABASE, 0x6A, BIT(6), BIT(6));
}

static void
via_hide_cursor(struct drm_crtc *crtc)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	uint32_t temp;

	if (iga->index) {
		temp = VIA_READ(HI_CONTROL);
		VIA_WRITE(HI_CONTROL, temp & 0xFFFFFFFA);
	} else {
		temp = VIA_READ(PRIM_HI_CTRL);
		VIA_WRITE(PRIM_HI_CTRL, temp & 0xFFFFFFFA);
	}
}

static void
via_show_cursor(struct drm_crtc *crtc)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;

	if (!iga->cursor_kmap.bo)
		return;

	/* Program the offset and turn on Hardware icon Cursor */
	if (iga->index) {
		VIA_WRITE(HI_FBOFFSET, iga->cursor_kmap.bo->offset);
		VIA_WRITE(HI_CONTROL, 0xB6000005);
	} else {
		VIA_WRITE(PRIM_HI_FBOFFSET, iga->cursor_kmap.bo->offset);
		VIA_WRITE(PRIM_HI_CTRL, 0x36000005);
	}
}

static int
via_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			uint32_t handle, uint32_t width, uint32_t height)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	int max_height = 64, max_width = 64, ret = 0, i;
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj = NULL;
	struct ttm_bo_kmap_obj user_kmap;

	if (!iga->cursor_kmap.bo)
		return -ENXIO;

	if (!handle) {
		/* turn off cursor */
		via_hide_cursor(crtc);
		return ret;
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
	if (!obj || !obj->driver_private) {
		DRM_ERROR("Cannot find cursor object %x for crtc %d\n", handle, crtc->base.id);
		return -ENOENT;
	}

	user_kmap.bo = obj->driver_private;
	ret = ttm_bo_kmap(user_kmap.bo, 0, user_kmap.bo->mem.size, &user_kmap);
	if (!ret) {
		/* Copy data from userland to cursor memory region */
		u32 *dst = iga->cursor_kmap.virtual, *src = user_kmap.virtual;

		memset_io(dst, 0x0, iga->cursor_kmap.bo->mem.size);
		for (i = 0; i < height; i++) {
			__iowrite32_copy(dst, src, width);
			dst += max_width;
			src += width;
		}
		ttm_bo_kunmap(&user_kmap);
	}
	drm_gem_object_unreference_unlocked(obj);
	via_show_cursor(crtc);
	return ret;
}

static int
via_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	unsigned char xoff = 0, yoff = 0;
	int xpos = x, ypos = y;

	if (x < 0) {
		xoff = (-x) & 0xFF;
		xpos = 0;
	}

	if (y < 0) {
		yoff = (-y) & 0xFF;
		ypos = 0;
	}

	if (iga->index) {
		VIA_WRITE(HI_POSSTART, ((xpos << 16) | (ypos & 0x07ff)));
		VIA_WRITE(HI_CENTEROFFSET, ((xoff << 16) | (yoff & 0x07ff)));
	} else {
		VIA_WRITE(PRIM_HI_POSSTART, ((xpos << 16) | (ypos & 0x07ff)));
		VIA_WRITE(PRIM_HI_CENTEROFFSET, ((xoff << 16) | (yoff & 0x07ff)));
	}
	return 0;
}

static void
via_iga1_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green, u16 *blue,
			uint32_t start, uint32_t size)
{
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	int end = (start + size > 256) ? 256 : start + size, i;
	u8 val, sr1a = vga_rseq(VGABASE, 0x1A);

	if (!crtc->enabled || !crtc->fb)
		return;

	if (crtc->fb->bits_per_pixel == 8) {
		/* Prepare for initialize IGA1's LUT: */
		vga_wseq(VGABASE, 0x1A, sr1a & 0xFE);
		/* Change to Primary Display's LUT */
		val = vga_rseq(VGABASE, 0x1B);
		vga_wseq(VGABASE, 0x1B, val);
		val = vga_rcrt(VGABASE, 0x67);
		vga_wcrt(VGABASE, 0x67, val);

		/* Fill in IGA1's LUT */
		for (i = start; i < end; i++) {
			/* Bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xFF);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		/* enable LUT */
		svga_wseq_mask(VGABASE, 0x1B, 0x00, BIT(0));
		/* Disable gamma in case it was enabled previously */
		svga_wcrt_mask(VGABASE, 0x33, 0x00, BIT(7));
		/* access Primary Display's LUT */
		vga_wseq(VGABASE, 0x1A, sr1a & 0xFE);
	} else {
		/* Enable Gamma */
		svga_wcrt_mask(VGABASE, 0x33, BIT(7), BIT(7));
		svga_wseq_mask(VGABASE, 0x1A, 0x00, BIT(0));

		/* Fill in IGA1's gamma */
		for (i = start; i < end; i++) {
			/* bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xFF);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		vga_wseq(VGABASE, 0x1A, sr1a);
	}
}

static void
via_iga2_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green, u16 *blue,
			uint32_t start, uint32_t size)
{
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	int end = (start + size > 256) ? 256 : start + size, i;
	u8 sr1a = vga_rseq(VGABASE, 0x1A);

	if (!crtc->enabled || !crtc->fb)
		return;

	if (crtc->fb->bits_per_pixel == 8) {
		/* Change Shadow to Secondary Display's LUT */
		svga_wseq_mask(VGABASE, 0x1A, BIT(0), BIT(0));
		/* Enable Secondary Display Engine */
		svga_wseq_mask(VGABASE, 0x1B, BIT(7), BIT(7));
		/* Second Display Color Depth, 8bpp */
		svga_wcrt_mask(VGABASE, 0x67, 0x3F, 0x3F);

		/* Enable second display channel just in case. */
		if (!(vga_rcrt(VGABASE, 0x6A) & BIT(7)))
			svga_wcrt_mask(VGABASE, 0x6A, BIT(7), BIT(7));

		/* Fill in IGA2's LUT */
		for (i = start; i < end; i++) {
			/* Bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xFF);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		/* Disable gamma in case it was enabled previously */
		svga_wcrt_mask(VGABASE, 0x6A, 0x00, BIT(1));

		/* access Primary Display's LUT */
		vga_wseq(VGABASE, 0x1A, sr1a & 0xFE);
	} else {
		svga_wseq_mask(VGABASE, 0x1A, BIT(0), BIT(0));
		/* Enable Gamma */
		svga_wcrt_mask(VGABASE, 0x6A, BIT(1), BIT(1));

		/* Before we fill the second LUT, we have to enable
		 * second display channel. If it's enabled before,
		 * we don't need to do that, or else the secondary
		 * display will be dark for about 1 sec and then be
		 * turned on again.
		 */
		if (!(vga_rcrt(VGABASE, 0x6A) & BIT(7)))
			enable_second_display_channel(dev_priv);

		/* Fill in IGA2's gamma */
		for (i = start; i < end; i++) {
			/* bit mask of palette */
			vga_w(VGABASE, VGA_PEL_MSK, 0xFF);
			vga_w(VGABASE, VGA_PEL_IW, i);
			vga_w(VGABASE, VGA_PEL_D, red[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, green[i] >> 8);
			vga_w(VGABASE, VGA_PEL_D, blue[i] >> 8);
		}
		/* access Primary Display's LUT */
		vga_wseq(VGABASE, 0x1A, sr1a);
	}
}

static void
via_crtc_destroy(struct drm_crtc *crtc)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);

	if (iga->cursor_kmap.bo) {
		ttm_bo_unpin(iga->cursor_kmap.bo, &iga->cursor_kmap);
		ttm_bo_unref(&iga->cursor_kmap.bo);
	}
	drm_crtc_cleanup(crtc);
}

static const struct drm_crtc_funcs via_iga1_funcs = {
	.cursor_set = via_crtc_cursor_set,
	.cursor_move = via_crtc_cursor_move,
	.gamma_set = via_iga1_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = via_crtc_destroy,
};

static const struct drm_crtc_funcs via_iga2_funcs = {
	.cursor_set = via_crtc_cursor_set,
	.cursor_move = via_crtc_cursor_move,
	.gamma_set = via_iga2_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = via_crtc_destroy,
};

static void
via_load_fifo_regs(struct via_crtc *iga, struct drm_display_mode *mode)
{
	u32 queue_expire_num = iga->display_queue_expire_num, reg_value;
	struct drm_via_private *dev_priv = iga->base.dev->dev_private;
	int hor_active = mode->hdisplay, ver_active = mode->vdisplay;
	struct drm_device *dev = iga->base.dev;

	/* If resolution > 1280x1024, expire length = 64, else
	   expire length = 128 */
	if ((dev->pdev->device == PCI_DEVICE_ID_VIA_K8M800 ||
	     dev->pdev->device == PCI_DEVICE_ID_VIA_CN700) &&
	    ((hor_active > 1280) && (ver_active > 1024)))
		queue_expire_num = 16;

	if (!iga->index) {
		/* Set IGA1 Display FIFO Depth Select */
		reg_value = IGA1_FIFO_DEPTH_SELECT_FORMULA(iga->fifo_max_depth);
		load_value_to_registers(VGABASE, &iga->fifo_depth, reg_value);
	} else {
		/* Set IGA2 Display FIFO Depth Select */
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

/* Load CRTC timing registers */
void via_load_crtc_timing(struct via_crtc *iga, struct drm_display_mode *mode)
{
	struct drm_via_private *dev_priv = iga->base.dev->dev_private;
	struct drm_device *dev = iga->base.dev;
	u32 reg_value = 0;

	if (!iga->index) {
		if (dev->pdev->device == PCI_DEVICE_ID_VIA_VX900) {
			/* Disable IGA1 shadow timing */
			svga_wcrt_mask(VGABASE, 0x45, 0x00, BIT(0));

			/* Disable IGA1 pixel timing */
			svga_wcrt_mask(VGABASE, 0xFD, 0x00, BIT(6) | BIT(5));
		}

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

static void
via_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;

	switch (mode) {
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		drm_vblank_pre_modeset(crtc->dev, iga->index);
		if (iga->index) {
			/* turn off CRT screen (IGA2) */
			svga_wcrt_mask(VGABASE, 0x6B, BIT(2), BIT(2));
			disable_second_display_channel(dev_priv);
			/* clear for TV clock */
			svga_wcrt_mask(VGABASE, 0x6C, 0x00, 0x0F);
		} else {
			/* turn off CRT screen (IGA1) */
			svga_wseq_mask(VGABASE, 0x01, BIT(5), BIT(5));
			/* clear for TV clock */
			svga_wcrt_mask(VGABASE, 0x6C, 0x00, 0xF0);
		}
		break;

	case DRM_MODE_DPMS_ON:
		drm_vblank_post_modeset(crtc->dev, iga->index);
		if (iga->index) {
			/* turn on CRT screen (IGA2) */
			enable_second_display_channel(dev_priv);
			svga_wcrt_mask(VGABASE, 0x6B, 0x00, BIT(2));
		} else {
			/* turn on CRT screen (IGA1) */
			svga_wseq_mask(VGABASE, 0x01, 0x00, BIT(5));
		}
		/* disable simultaneous  */
		svga_wcrt_mask(VGABASE, 0x6B, 0x00, BIT(3));
		drm_mode_crtc_load_lut(crtc);
		break;
	}
}

static void
via_crtc_prepare(struct drm_crtc *crtc)
{
	/* Turn off the cursor */
	via_hide_cursor(crtc);

	/* Blank the screen */
	if (crtc->enabled)
		via_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void
via_crtc_commit(struct drm_crtc *crtc)
{
	/* Turn on the cursor */
	via_show_cursor(crtc);

	/* Turn on the monitor */
	if (crtc->enabled)
		via_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool
via_crtc_mode_fixup(struct drm_crtc *crtc, const struct drm_display_mode *mode,
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

	if (!fb)
		fb = old_fb;

	vga_r(VGABASE, VGA_IS1_RC);
	vga_w(VGABASE, VGA_ATT_IW, 0x00);

	/* Write Misc Register */
	vga_w(VGABASE, VGA_MIS_W, 0xC7);

	svga_wseq_mask(VGABASE, 0x15, 0xA2, 0xA2);

	regs_init(VGABASE);

	via_unlock_crt(VGABASE, dev->pdev->device);

	if (!iga->index) {
		vga_wcrt(VGABASE, 0x09, 0x00);	/*initial CR09=0 */
		svga_wcrt_mask(VGABASE, 0x11, 0x00, 0x70);
		svga_wcrt_mask(VGABASE, 0x17, 0x00, BIT(7));
        }

	/* Write CRTC */
	via_load_crtc_timing(iga, adjusted_mode);

	via_lock_crt(VGABASE);
	svga_wcrt_mask(VGABASE, 0x17, BIT(7), BIT(7));

	/* Load FIFO */
	if ((dev->pdev->device != PCI_DEVICE_ID_VIA_CLE266) &&
	    (dev->pdev->device != PCI_DEVICE_ID_VIA_KM400)) {
		via_load_fifo_regs(iga, adjusted_mode);
	} else if (adjusted_mode->hdisplay == 1024 &&
		   adjusted_mode->vdisplay == 768) {
		/* Update Patch Register */
		svga_wseq_mask(VGABASE, 0x16, 0x0C, 0xBF);
		vga_wseq(VGABASE, 0x18, 0x4C);
	}
	vga_r(VGABASE, VGA_IS1_RC);
	vga_w(VGABASE, VGA_ATT_IW, 0x20);

	via_set_pll(crtc, adjusted_mode);

	return crtc_funcs->mode_set_base(crtc, crtc->x, crtc->y, old_fb);
}

static int
via_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	enum mode_set_atomic state = old_fb ? LEAVE_ATOMIC_MODE_SET : ENTER_ATOMIC_MODE_SET;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	struct drm_framebuffer *new_fb = crtc->fb;
	struct ttm_buffer_object *bo;
	struct drm_gem_object *obj;
	int ret = 0;

	/* no fb bound */
	if (!new_fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return ret;
	}

	obj = new_fb->helper_private;
	bo = obj->driver_private;

	ret = ttm_bo_pin(bo, NULL);
	if (likely(ret == 0))
		ret = crtc_funcs->mode_set_base_atomic(crtc, new_fb, x, y, state);

	/* Free the old framebuffer if it exist */
	if (old_fb) {
		obj = old_fb->helper_private;
		bo = obj->driver_private;

		ret = ttm_bo_unpin(bo, NULL);
		if (unlikely(ret))
			DRM_ERROR("framebuffer still locked\n");
	}
	return ret;
}

static int
via_iga1_mode_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
				int x, int y, enum mode_set_atomic state)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	struct drm_gem_object *obj = fb->helper_private;
	struct ttm_buffer_object *bo = obj->driver_private;
	u32 pitch = (x * fb->bits_per_pixel) >> 3, addr;
	u8 value;

	/*if ((state == ENTER_ATOMIC_MODE_SET) && (fb != crtc->fb))
		disable_accel(dev);
	else
		restore_accel(dev);*/

	/* Set the framebuffer offset */
	pitch += (y * fb->pitches[0]);
	addr = round_up(bo->offset + pitch, 16) >> 1;

	vga_wcrt(VGABASE, 0x0D, addr & 0xFF);
	vga_wcrt(VGABASE, 0x0C, (addr >> 8) & 0xFF);
	vga_wcrt(VGABASE, 0x34, (addr >> 16) & 0xFF);
	value = (vga_rcrt(VGABASE, 0x48) & ~0x1F);
	vga_wcrt(VGABASE, 0x48, ((addr >> 24) & 0x1F) | value);

	/* Load Fetch registers */
	pitch = ALIGN((crtc->mode.hdisplay * fb->bits_per_pixel >> 3), 16) >> 4;
	load_value_to_registers(VGABASE, &iga->fetch, pitch + 1);

	if ((state == ENTER_ATOMIC_MODE_SET) ||
	     crtc->fb->pitches[0] != fb->pitches[0]) {
		/* Spec does not say that first adapter skips 3 bits but old
		 * code did it and seems to be reasonable in analogy to
		 * second adapter */
		load_value_to_registers(VGABASE, &iga->offset,
					ALIGN(fb->pitches[0], 16) >> 3);
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
		svga_wseq_mask(VGABASE, 0x15, value, 0x1C);
	}
	return 0;
}

static int
via_iga2_mode_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
				int x, int y, enum mode_set_atomic state)
{
	struct via_crtc *iga = container_of(crtc, struct via_crtc, base);
	struct drm_via_private *dev_priv = crtc->dev->dev_private;
	struct drm_gem_object *obj = fb->helper_private;
	struct ttm_buffer_object *bo = obj->driver_private;
	u32 pitch = (x * fb->bits_per_pixel) >> 3, addr;
	u8 value;

	/* Set the framebuffer offset */
	pitch += y * fb->pitches[0];
	addr = round_up(bo->offset + pitch, 16);

	/* Secondary display supports only quadword aligned memory */
	vga_wcrt(VGABASE, 0x62, (addr >> 2) & 0xfe);
	vga_wcrt(VGABASE, 0x63, (addr >> 10) & 0xff);
	vga_wcrt(VGABASE, 0x64, (addr >> 18) & 0xff);
	value = (vga_rcrt(VGABASE, 0xA3) & ~0x07);
	vga_wcrt(VGABASE, 0xA3, ((addr >> 26) & 0x07) | value);

	/* Load Fetch registers */
	pitch = ALIGN((crtc->mode.hdisplay * fb->bits_per_pixel >> 3), 16) >> 4;
	load_value_to_registers(VGABASE, &iga->fetch, pitch);

	if ((state == ENTER_ATOMIC_MODE_SET) ||
	     crtc->fb->pitches[0] != fb->pitches[0]) {
		/* Set secondary pitch */
		load_value_to_registers(VGABASE, &iga->offset,
					ALIGN(fb->pitches[0], 16) >> 3);
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
		svga_wseq_mask(VGABASE, 0x67, value, 0xC0);
	}
	return 0;
}

static const struct drm_crtc_helper_funcs via_iga1_helper_funcs = {
	.dpms = via_crtc_dpms,
	.prepare = via_crtc_prepare,
	.commit = via_crtc_commit,
	.mode_fixup = via_crtc_mode_fixup,
	.mode_set = via_crtc_mode_set,
	.mode_set_base = via_crtc_mode_set_base,
	.mode_set_base_atomic = via_iga1_mode_set_base_atomic,
	.load_lut = drm_mode_crtc_load_lut,
};

static const struct drm_crtc_helper_funcs via_iga2_helper_funcs = {
	.dpms = via_crtc_dpms,
	.prepare = via_crtc_prepare,
	.commit = via_crtc_commit,
	.mode_fixup = via_crtc_mode_fixup,
	.mode_set = via_crtc_mode_set,
	.mode_set_base = via_crtc_mode_set_base,
	.mode_set_base_atomic = via_iga2_mode_set_base_atomic,
	.load_lut = drm_mode_crtc_load_lut,
};

void __devinit
via_crtc_init(struct drm_device *dev, int index)
{
	struct drm_via_private *dev_priv = dev->dev_private;
	struct via_crtc *iga = &dev_priv->iga[index];
	struct drm_crtc *crtc = &iga->base;
	int cursor_size = 64 * 64 * 4, i;
	u16 *gamma;

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
		if (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900)
			iga->timings.hdisplay.count--;

		iga->timings.hblank_start.count = ARRAY_SIZE(iga2_hor_blank_start);
		iga->timings.hblank_start.regs = iga2_hor_blank_start;
		if (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900)
			iga->timings.hblank_start.count--;

		iga->timings.hblank_end.count = ARRAY_SIZE(iga2_hor_blank_end);
		iga->timings.hblank_end.regs = iga2_hor_blank_end;

		iga->timings.hsync_start.count = ARRAY_SIZE(iga2_hor_sync_start);
		iga->timings.hsync_start.regs = iga2_hor_sync_start;
		if (dev->pdev->device == PCI_DEVICE_ID_VIA_CLE266 ||
		    dev->pdev->device == PCI_DEVICE_ID_VIA_KM400)
			iga->timings.hsync_start.count--;

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

		/* Secondary FIFO setup */
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

		/* Older hardware only uses 12 bits */
		iga->offset.count = ARRAY_SIZE(iga2_offset) - 1;
		iga->offset.regs = iga2_offset;

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
			iga->offset.count++;
			break;

		// VX855
		case PCI_DEVICE_ID_VIA_VX875:
			iga->fifo_high_threshold = iga->fifo_threshold = 160;
			iga->display_queue_expire_num = 320;
			iga->fifo_max_depth = 200;
			iga->offset.count++;
			break;

		// VX900
		case PCI_DEVICE_ID_VIA_VX900:
			iga->fifo_high_threshold = iga->fifo_threshold = 160;
			iga->display_queue_expire_num = 320;
			iga->fifo_max_depth = 192;
			iga->offset.count++;
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
		if (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900)
			iga->timings.hdisplay.count--;

		iga->timings.hblank_start.count = ARRAY_SIZE(iga1_hor_blank_start);
		iga->timings.hblank_start.regs = iga1_hor_blank_start;
		if (dev->pdev->device != PCI_DEVICE_ID_VIA_VX900)
			iga->timings.hblank_start.count--;

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

		/* Primary FIFO setup */
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

		iga->offset.count = ARRAY_SIZE(iga1_offset);
		iga->offset.regs = iga1_offset;

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
	}
	drm_mode_crtc_set_gamma_size(crtc, 256);
	gamma = crtc->gamma_store;

	for (i = 0; i < 256; i++) {
		gamma[i] = i << 8 | i;
		gamma[i+256] = i << 8 | i;
		gamma[i+512] = i << 8 | i;
	}

	if (dev->pdev->device == PCI_DEVICE_ID_VIA_CLE266 ||
	    dev->pdev->device == PCI_DEVICE_ID_VIA_KM400)
		cursor_size = 32 * 32 * 4;

	if (ttm_allocate_kernel_buffer(&dev_priv->bdev, cursor_size, 16,
					TTM_PL_FLAG_VRAM, &iga->cursor_kmap))
		DRM_ERROR("failed to create cursor\n");
}
