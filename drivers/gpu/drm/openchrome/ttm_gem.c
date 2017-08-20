/*
 * Copyright 2012 James Simmons <jsimmons@infradead.org>. All Rights Reserved.
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
#include "drmP.h"
#include "via_drv.h"

/*
 * initialize the gem buffer object
 */
int ttm_gem_open_object(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	return 0;
}

/*
 * free the gem buffer object
 */
void ttm_gem_free_object(struct drm_gem_object *obj)
{
	struct ttm_gem_object *gem = container_of(obj, struct ttm_gem_object, gem);

	if (gem->heap != NULL) {
		struct ttm_buffer_object *bo = &gem->heap->pbo;

		ttm_bo_unref(&bo);
		gem->heap = NULL;
	}
	drm_gem_object_release(obj);
	kfree(gem);
}

struct ttm_buffer_object *
ttm_gem_mapping(struct drm_gem_object *obj)
{
	struct ttm_gem_object *gem;

	gem = container_of(obj, struct ttm_gem_object, gem);
	if (gem->heap == NULL)
		return NULL;
	return &gem->heap->pbo;
}

/*
 * file operation mmap
 */
int ttm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct via_device *dev_priv;
	struct drm_file *file_priv;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	dev_priv = file_priv->minor->dev->dev_private;
	if (!dev_priv)
		return -EINVAL;

	return ttm_bo_mmap(filp, vma, &dev_priv->bdev);
}

struct drm_gem_object *
ttm_gem_create(struct drm_device *dev, struct ttm_bo_device *bdev,
	       enum ttm_bo_type origin, int types, bool interruptible,
	       int byte_align, int page_align, unsigned long size)
{
	struct ttm_buffer_object *bo = NULL;
	struct ttm_gem_object *obj;
	int ret;

	size = round_up(size, byte_align);
	size = ALIGN(size, page_align);

	ret = via_bo_create(bdev, size, origin, types, byte_align,
			      page_align, interruptible, NULL, NULL, &bo);
	if (ret) {
		DRM_ERROR("Failed to create buffer object\n");
		return ERR_PTR(ret);
	}

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		ttm_bo_unref(&bo);
		return ERR_PTR(-ENOMEM);
	}

	ret = drm_gem_object_init(dev, &obj->gem, size);
	if (unlikely(ret)) {
		ttm_bo_unref(&bo);
		return ERR_PTR(ret);
	}

	obj->heap = container_of(bo, struct ttm_heap, pbo);
	bo->persistent_swap_storage = obj->gem.filp;
	return &obj->gem;
}
