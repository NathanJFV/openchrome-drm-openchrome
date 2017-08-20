/*
 * Copyright 2014 James Simmons <jsimmons@infradead.org>
 * Influenced by sample code from VIA Technologies and the radeon driver.
 *
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
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
#ifndef _VIA_DRV_H_
#define _VIA_DRV_H_

#define DRIVER_AUTHOR       "The OpenChrome Project"
#define DRIVER_NAME         "openchrome"
#define DRIVER_DESC         "OpenChrome DRM for VIA Technologies Chrome IGP"
#define DRIVER_DATE         "20170726"

#define DRIVER_MAJOR		3
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	43

#include <linux/module.h>

#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include "ttm/ttm_memory.h"
#include "ttm/ttm_module.h"
#include "ttm/ttm_page_alloc.h"

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <drm/via_drm.h>

#include "via_regs.h"
#include "via_fence.h"
#include "via_dma.h"
#include "via_verifier.h"
#include "via_display.h"

#define VIA_MM_ALIGN_SIZE 16

#define VIA_PCI_BUF_SIZE 60000
#define VIA_FIRE_BUF_SIZE  1024
#define VIA_NUM_IRQS 4

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#define CLE266_REVISION_AX	0x0A
#define CLE266_REVISION_CX	0x0C
#define CX700_REVISION_700	0x0
#define CX700_REVISION_700M	0x1
#define CX700_REVISION_700M2	0x2

/* For VT3353 */
#define VX800_REVISION_A	0x0f
#define VX800_REVISION_B0	0x10
#define VX800_REVISION_B1	0x11
#define VX800_REVISION_B2	0x12

/* For VT3409 */
#define VX855_REVISION_A0	0x00
#define VX855_REVISION_A1	0x01
#define VX855_REVISION_A2	0x02

/* For VT3410 */
#define VX900_REVISION_A0	0x00
#define VX900_REVISION_A1	0x01
#define VX900_REVISION_A2	0x02
#define VX900_REVISION_A3	0x03

typedef uint32_t maskarray_t[5];

typedef struct drm_via_irq {
	atomic_t irq_received;
	uint32_t pending_mask;
	uint32_t enable_mask;
	wait_queue_head_t irq_queue;
} drm_via_irq_t;

struct sgdma_tt {
	struct ttm_dma_tt sgdma;
	unsigned long offset;
};

struct via_state {
	struct vga_regset crt_regs[256];
	struct vga_regset seq_regs[256];
};

struct ttm_heap {
    struct ttm_place busy_placements[TTM_NUM_MEM_TYPES];
    struct ttm_place placements[TTM_NUM_MEM_TYPES];
    struct ttm_buffer_object pbo;
};

struct ttm_gem_object {
	struct drm_gem_object gem;
	struct ttm_heap *heap;
};

struct ttm_fb_helper {
	struct drm_fb_helper base;
	struct ttm_bo_kmap_obj kmap;
};

enum via_engine {
	VIA_ENG_H1 = 0,
	VIA_ENG_H2,
	VIA_ENG_H5S1,
	VIA_ENG_H5S2VP1,
	VIA_ENG_H6S2
};

struct via_device {
	struct drm_global_reference mem_global_ref;
	struct ttm_bo_global_ref bo_global_ref;
	struct ttm_bo_device bdev;
	struct drm_device *dev;
	int revision;

	struct ttm_bo_kmap_obj dmabuf;
	struct ttm_bo_kmap_obj mmio;
	struct ttm_bo_kmap_obj gart;
	struct ttm_bo_kmap_obj vq;

	struct drm_fb_helper *helper;
    u8 vram_type;
    unsigned long long vram_start;
    unsigned int vram_size;
    int vram_mtrr;

	struct via_state pm_cache;

	enum via_engine engine_type;
	struct drm_via_state hc_state;
	unsigned int dma_low;
	unsigned int dma_high;
	unsigned int dma_offset;
	uint32_t dma_diff;
	uint32_t dma_wrap;
	void __iomem *last_pause_ptr;
	void __iomem *hw_addr_ptr;

	char pci_buf[VIA_PCI_BUF_SIZE];
	const uint32_t *fire_offsets[VIA_FIRE_BUF_SIZE];
	uint32_t num_fire_offsets;

	drm_via_irq_t via_irqs[VIA_NUM_IRQS];
	struct work_struct hotplug_work;
	struct workqueue_struct *wq;
	unsigned num_irqs;
	maskarray_t *irq_masks;
	uint32_t irq_enable_mask;
	uint32_t irq_pending_mask;
	int *irq_map;

	/* fence handling */
	struct via_fence_pool *dma_fences;
	int desc_size;

	struct via_crtc iga[2];
	bool spread_spectrum;
};

#define VIA_MEM_NONE		0x00
#define VIA_MEM_SDR66		0x01
#define VIA_MEM_SDR100		0x02
#define VIA_MEM_SDR133		0x03
#define VIA_MEM_DDR_200		0x04
#define VIA_MEM_DDR_266		0x05
#define VIA_MEM_DDR_333		0x06
#define VIA_MEM_DDR_400		0x07
#define VIA_MEM_DDR2_400	0x08
#define VIA_MEM_DDR2_533	0x09
#define VIA_MEM_DDR2_667	0x0A
#define VIA_MEM_DDR2_800	0x0B
#define VIA_MEM_DDR2_1066	0x0C
#define VIA_MEM_DDR3_533	0x0D
#define VIA_MEM_DDR3_667	0x0E
#define VIA_MEM_DDR3_800	0x0F
#define VIA_MEM_DDR3_1066	0x10
#define VIA_MEM_DDR3_1333	0x11
#define VIA_MEM_DDR3_1600	0x12

/* VIA MMIO register access */
#define VIA_BASE ((dev_priv->mmio.virtual))

#define VIA_READ(reg)		ioread32(VIA_BASE + reg)
#define VIA_WRITE(reg, val)	iowrite32(val, VIA_BASE + reg)
#define VIA_READ8(reg)		ioread8(VIA_BASE + reg)
#define VIA_WRITE8(reg, val)	iowrite8(val, VIA_BASE + reg)

#define VIA_WRITE_MASK(reg, data, mask)  \
	VIA_WRITE(reg, (data & mask) | (VIA_READ(reg) & ~mask)) \

#define VGABASE (VIA_BASE+VIA_MMIO_VGABASE)

extern const struct drm_ioctl_desc via_ioctls[];
extern int via_max_ioctl;

extern int via_hdmi_audio;

extern void via_engine_init(struct drm_device *dev);

extern int via_dma_init(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_flush_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_dispatch_cmdbuffer(struct drm_device *dev, drm_via_cmdbuffer_t *cmd);
extern int via_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_cmdbuf_size(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_pci_cmdbuffer(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_wait_irq(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int via_wait_idle(struct via_device *dev_priv);

extern int via_vram_init(struct via_device *dev_priv);

extern int via_enable_vblank(struct drm_device *dev, int crtc);
extern void via_disable_vblank(struct drm_device *dev, int crtc);

extern irqreturn_t via_driver_irq_handler(int irq, void *arg);
extern void via_driver_irq_preinstall(struct drm_device *dev);
extern int via_driver_irq_postinstall(struct drm_device *dev);
extern void via_driver_irq_uninstall(struct drm_device *dev);

extern void via_init_command_verifier(void);
extern int via_driver_dma_quiescent(struct drm_device *dev);
extern int via_dma_cleanup(struct drm_device *dev);

extern void via_dmablit_handler(struct drm_device *dev, int engine, int from_irq);
extern int via_dmablit_init(struct drm_device *dev);

extern int via_mm_init(struct via_device *dev_priv);
void via_mm_fini(struct drm_device *dev);
extern void ttm_placement_from_domain(struct ttm_buffer_object *bo,
                      struct ttm_placement *placement,
                      u32 domains, struct ttm_bo_device *bdev);
extern int via_bo_create(struct ttm_bo_device *bdev, unsigned long size,
               enum ttm_bo_type origin, uint32_t domains,
               uint32_t byte_align, uint32_t page_align,
               bool interruptible, struct sg_table *sg,
               struct reservation_object *resv,
               struct ttm_buffer_object **p_bo);
extern int via_bo_pin(struct ttm_buffer_object *bo, struct ttm_bo_kmap_obj *kmap);
extern int via_bo_unpin(struct ttm_buffer_object *bo, struct ttm_bo_kmap_obj *kmap);
extern int via_ttm_allocate_kernel_buffer(struct ttm_bo_device *bdev, unsigned long size,
                      uint32_t alignment, uint32_t domain,
                      struct ttm_bo_kmap_obj *kmap);


extern int ttm_mmap(struct file *filp, struct vm_area_struct *vma);

extern int ttm_gem_open_object(struct drm_gem_object *obj, struct drm_file *file_priv);
extern void ttm_gem_free_object(struct drm_gem_object *obj);
extern struct drm_gem_object *ttm_gem_create(struct drm_device *dev,
                         struct ttm_bo_device *bdev,
                         enum ttm_bo_type origin,
                         int type, bool interruptible,
                         int byte_align, int page_align,
                         unsigned long size);
extern struct ttm_buffer_object *ttm_gem_mapping(struct drm_gem_object *obj);

extern struct ttm_tt *
via_sgdma_backend_init(struct ttm_bo_device *bdev, unsigned long size,
               uint32_t page_flags, struct page *dummy_read_page);

#endif
