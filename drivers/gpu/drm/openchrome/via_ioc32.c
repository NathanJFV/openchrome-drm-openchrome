/*
 * Copyright 2006 Tungsten Graphics Inc., Bismarck, ND., USA.
 * All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */
#include "drmP.h"
#include "via_drv.h"

static int
via_getparam(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	struct via_device *dev_priv = dev->dev_private;
	struct drm_via_param *args = data;
	int ret = 0;

	switch (args->param) {
	case VIA_PARAM_CHIPSET_ID:
		args->value = dev->pdev->device;
		break;
	case VIA_PARAM_REVISION_ID:
		args->value = dev_priv->revision;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

/* Not yet supported */
static int
via_setparam(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	return -EINVAL;
}

static int
via_gem_alloc(struct drm_device *dev, void *data,
		struct drm_file *filp)
{
	struct via_device *dev_priv = dev->dev_private;
	struct drm_via_gem_object *args = data;
	struct drm_gem_object *obj;
	int ret = -ENOMEM;

	obj = ttm_gem_create(dev, &dev_priv->ttm.bdev, args->size,
				ttm_bo_type_device, args->domains,
				args->alignment, PAGE_SIZE, false);
	if (obj != NULL) {
		ret = drm_gem_handle_create(filp, obj, &args->handle);
		/* drop reference from allocate - handle holds it now */
		drm_gem_object_unreference_unlocked(obj);
		if (!ret) {
			struct ttm_buffer_object *bo = ttm_gem_mapping(obj);

			args->map_handle = drm_vma_node_offset_addr(&bo->vma_node);
			args->domains = bo->mem.placement & TTM_PL_MASK_MEM;
			args->offset = bo->offset;
			args->size = bo->mem.size;
			args->version = 1;
			obj->read_domains = obj->write_domain = args->domains;
		}
	}
	return ret;
}

static int
via_gem_state(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_via_gem_object *args = data;
	struct ttm_buffer_object *bo = NULL;
	struct drm_gem_object *obj = NULL;
	struct ttm_placement placement;
	int ret = -EINVAL;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return ret;

	bo = ttm_gem_mapping(obj);
	if (bo == NULL)
		return ret;

	/* Don't bother to migrate to same domain */
	args->domains &= ~(bo->mem.placement & TTM_PL_MASK_MEM);
	if (args->domains) {
		ret = ttm_bo_reserve(bo, true, false, false, 0);
		if (unlikely(ret))
			return ret;

		ttm_placement_from_domain(bo, &placement, args->domains, bo->bdev);
		ret = ttm_bo_validate(bo, &placement, false, false);
		ttm_bo_unreserve(bo);

		if (!ret) {
			args->map_handle = drm_vma_node_offset_addr(&bo->vma_node);
			args->domains = bo->mem.placement & TTM_PL_MASK_MEM;
			args->offset = bo->offset;
			args->size = bo->mem.size;

			obj->read_domains = obj->write_domain = args->domains;
		}
	}
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static int
via_gem_wait(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_via_gem_wait *args = data;
	struct ttm_buffer_object *bo;
	struct drm_gem_object *obj;
	int ret = -EINVAL;
	bool no_wait;

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (obj == NULL)
		return ret;

	bo = ttm_gem_mapping(obj);
	if (bo == NULL)
		return ret;

	no_wait = (args->no_wait != 0);
	ret = ttm_bo_reserve(bo, true, no_wait, false, 0);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_wait(bo, true, true, no_wait);
	ttm_bo_unreserve(bo);

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

#define KMS_INVALID_IOCTL(name)						\
int name(struct drm_device *dev, void *data, struct drm_file *file_priv)\
{									\
	DRM_ERROR("invalid ioctl with kms %s\n", __func__);		\
	return -EINVAL;							\
}

KMS_INVALID_IOCTL(via_mem_alloc)
KMS_INVALID_IOCTL(via_mem_free)
KMS_INVALID_IOCTL(via_agp_init)
KMS_INVALID_IOCTL(via_fb_init)
KMS_INVALID_IOCTL(via_map_init)
KMS_INVALID_IOCTL(via_decoder_futex)
KMS_INVALID_IOCTL(via_dma_blit)
KMS_INVALID_IOCTL(via_dma_blit_sync)

const struct drm_ioctl_desc via_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIA_ALLOCMEM, via_mem_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FREEMEM, via_mem_free, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_AGP_INIT, via_agp_init, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_FB_INIT, via_fb_init, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_MAP_INIT, via_map_init, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_DEC_FUTEX, via_decoder_futex, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_OLD_GEM_CREATE, via_gem_alloc, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIA_DMA_INIT, via_dma_init, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUFFER, via_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FLUSH, via_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_PCICMD, via_pci_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUF_SIZE, via_cmdbuf_size, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_WAIT_IRQ, via_wait_irq, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_BLIT, via_dma_blit, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_BLIT_SYNC, via_dma_blit_sync, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_GETPARAM, via_getparam, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIA_SETPARAM, via_setparam, DRM_AUTH | DRM_MASTER | DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(VIA_GEM_CREATE, via_gem_alloc, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIA_GEM_WAIT, via_gem_wait, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_GEM_STATE, via_gem_state, DRM_AUTH),
};

int via_max_ioctl = ARRAY_SIZE(via_ioctls);
