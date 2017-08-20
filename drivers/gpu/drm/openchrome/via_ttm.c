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
#include <linux/dma-mapping.h>
#ifdef CONFIG_SWIOTLB
#include <linux/swiotlb.h>
#endif

#include "via_drv.h"

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static int
via_ttm_mem_global_init(struct drm_global_reference *ref)
{
    return ttm_mem_global_init(ref->object);
}

static void
via_ttm_mem_global_release(struct drm_global_reference *ref)
{
    ttm_mem_global_release(ref->object);
}

static int
via_ttm_global_init(struct via_device *dev_priv)
{
    struct drm_global_reference *global_ref;
    struct drm_global_reference *bo_ref;
    int rc;

    global_ref = &dev_priv->mem_global_ref;
    global_ref->global_type = DRM_GLOBAL_TTM_MEM;
    global_ref->size = sizeof(struct ttm_mem_global);
    global_ref->init = &via_ttm_mem_global_init;
    global_ref->release = &via_ttm_mem_global_release;

    rc = drm_global_item_ref(global_ref);
    if (unlikely(rc != 0)) {
        DRM_ERROR("Failed setting up TTM memory accounting\n");
        global_ref->release = NULL;
        return rc;
    }

    dev_priv->bo_global_ref.mem_glob = dev_priv->mem_global_ref.object;
    bo_ref = &dev_priv->bo_global_ref.ref;
    bo_ref->global_type = DRM_GLOBAL_TTM_BO;
    bo_ref->size = sizeof(struct ttm_bo_global);
    bo_ref->init = &ttm_bo_global_init;
    bo_ref->release = &ttm_bo_global_release;

    rc = drm_global_item_ref(bo_ref);
    if (unlikely(rc != 0)) {
        DRM_ERROR("Failed setting up TTM BO subsystem\n");
        drm_global_item_unref(global_ref);
        global_ref->release = NULL;
        return rc;
    }

    return rc;
}

static void
via_ttm_global_release(struct drm_global_reference *global_ref,
            struct ttm_bo_global_ref *global_bo,
            struct ttm_bo_device *bdev)
{
    DRM_DEBUG("Entered via_ttm_global_release.\n");

    if (global_ref->release == NULL)
        return;

    drm_global_item_unref(&global_bo->ref);
    drm_global_item_unref(global_ref);
    global_ref->release = NULL;

    DRM_DEBUG("Exiting via_ttm_global_release.\n");
}

static void
via_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
    struct ttm_heap *heap = container_of(bo, struct ttm_heap, pbo);

    kfree(heap);
    heap = NULL;
}

static struct ttm_tt *
via_ttm_tt_create(struct ttm_bo_device *bdev, unsigned long size,
			uint32_t page_flags, struct page *dummy_read_page)
{
#if IS_ENABLED(CONFIG_AGP)
	struct via_device *dev_priv =
		container_of(bdev, struct via_device, bdev);

	if (drm_pci_device_is_agp(dev_priv->dev))
		return ttm_agp_tt_create(bdev, dev_priv->dev->agp->bridge,
					size, page_flags, dummy_read_page);
#endif
	return via_sgdma_backend_init(bdev, size, page_flags, dummy_read_page);
}

static int
via_ttm_tt_populate(struct ttm_tt *ttm)
{
	struct sgdma_tt *dma_tt = (struct sgdma_tt *) ttm;
	struct ttm_dma_tt *sgdma = &dma_tt->sgdma;
	struct ttm_bo_device *bdev = ttm->bdev;
	struct via_device *dev_priv =
		container_of(bdev, struct via_device, bdev);
	struct drm_device *dev = dev_priv->dev;
	unsigned int i;
	int ret = 0;

	if (ttm->state != tt_unpopulated)
		return 0;

#if IS_ENABLED(CONFIG_AGP)
	if (drm_pci_device_is_agp(dev_priv->dev))
		return ttm_agp_tt_populate(ttm);
#endif

#ifdef CONFIG_SWIOTLB
	if (swiotlb_nr_tbl())
		return ttm_dma_populate(sgdma, dev->dev);
#endif

	ret = ttm_pool_populate(ttm);
	if (ret)
		return ret;

	for (i = 0; i < ttm->num_pages; i++) {
		sgdma->dma_address[i] = pci_map_page(dev->pdev, ttm->pages[i],
							0, PAGE_SIZE,
							PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(dev->pdev, sgdma->dma_address[i])) {
			while (--i) {
				pci_unmap_page(dev->pdev, sgdma->dma_address[i],
						PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
				sgdma->dma_address[i] = 0;
			}
			ttm_pool_unpopulate(ttm);
			return -EFAULT;
		}
	}
	return ret;
}

static void
via_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	struct sgdma_tt *dma_tt = (struct sgdma_tt *) ttm;
	struct ttm_dma_tt *sgdma = &dma_tt->sgdma;
	struct ttm_bo_device *bdev = ttm->bdev;
	struct via_device *dev_priv =
		container_of(bdev, struct via_device, bdev);
	struct drm_device *dev = dev_priv->dev;
	unsigned int i;

#if IS_ENABLED(CONFIG_AGP)
	if (drm_pci_device_is_agp(dev_priv->dev)) {
		ttm_agp_tt_unpopulate(ttm);
		return;
	}
#endif

#ifdef CONFIG_SWIOTLB
	if (swiotlb_nr_tbl()) {
		ttm_dma_unpopulate(sgdma, dev->dev);
		return;
	}
#endif

	for (i = 0; i < ttm->num_pages; i++) {
		if (sgdma->dma_address[i]) {
			pci_unmap_page(dev->pdev, sgdma->dma_address[i],
					PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
		}
	}

	ttm_pool_unpopulate(ttm);
}

static int
via_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	/*
	 * FIXME: Invalidate texture caches here.
	 */
	return 0;
}

static int
via_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
		struct ttm_mem_type_manager *man)
{
#if IS_ENABLED(CONFIG_AGP)
	struct via_device *dev_priv =
		container_of(bdev, struct via_device, bdev);
	struct drm_device *dev = dev_priv->dev;
#endif

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;

	case TTM_PL_TT:
		man->func = &ttm_bo_manager_func;

		/* By default we handle PCI/PCIe DMA. If AGP is avaliable
		 * then we use that instead */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE | TTM_MEMTYPE_FLAG_CMA;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;

#if IS_ENABLED(CONFIG_AGP)
		if (drm_pci_device_is_agp(dev) && dev->agp != NULL) {
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
			man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
			man->default_caching = TTM_PL_FLAG_WC;
		} else
			DRM_ERROR("AGP is possible but not enabled\n");
#endif
		break;

	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->func = &ttm_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		/* The display base address does not always equal the start of
		 * the memory region of the VRAM. In our case it is */
		man->gpu_offset = 0;
		break;

	case TTM_PL_PRIV0:
		/* MMIO region */
		man->func = &ttm_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED;
		man->default_caching = TTM_PL_FLAG_UNCACHED;
		break;

	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void
via_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *placement)
{
	switch (bo->mem.mem_type) {
	case TTM_PL_VRAM:
		ttm_placement_from_domain(bo, placement, TTM_PL_FLAG_TT | TTM_PL_FLAG_SYSTEM, bo->bdev);
		break;

	case TTM_PL_TT:
	default:
		ttm_placement_from_domain(bo, placement, TTM_PL_FLAG_SYSTEM, bo->bdev);
		break;
	}
}

/*
 * Allocate DMA capable memory for the blit descriptor chain, and an array that keeps
 * track of the pages we allocate. We don't want to use kmalloc for the descriptor
 * chain because it may be quite large for some blits, and pages don't need to be
 * contingous.
 */
struct drm_via_sg_info *
via_alloc_desc_pages(struct ttm_tt *ttm, struct drm_device *dev,
			unsigned long dev_start, enum dma_data_direction direction)
{
	struct drm_via_sg_info *vsg = kzalloc(sizeof(*vsg), GFP_KERNEL);
	struct via_device *dev_priv = dev->dev_private;
	int desc_size = dev_priv->desc_size, i;

	vsg->ttm = ttm;
	vsg->dev_start = dev_start;
	vsg->direction = direction;
	vsg->num_desc = ttm->num_pages;
	vsg->descriptors_per_page = PAGE_SIZE / desc_size;
	vsg->num_desc_pages = (vsg->num_desc + vsg->descriptors_per_page - 1) /
				vsg->descriptors_per_page;

	vsg->desc_pages = kzalloc(vsg->num_desc_pages * sizeof(void *), GFP_KERNEL);
	if (!vsg->desc_pages)
		return ERR_PTR(-ENOMEM);

	vsg->state = dr_via_desc_pages_alloc;

	/* Alloc pages for descriptor chain */
	for (i = 0; i < vsg->num_desc_pages; ++i) {
		vsg->desc_pages[i] = (unsigned long *) __get_free_page(GFP_KERNEL);

		if (!vsg->desc_pages[i])
			return ERR_PTR(-ENOMEM);
	}
	return vsg;
}

/* Move between GART and VRAM */
static int
via_move_blit(struct ttm_buffer_object *bo, bool evict, bool no_wait_gpu,
		struct ttm_mem_reg *new_mem, struct ttm_mem_reg *old_mem)
{
	struct via_device *dev_priv =
		container_of(bo->bdev, struct via_device, bdev);
	enum dma_data_direction direction = DMA_TO_DEVICE;
	unsigned long old_start, new_start, dev_addr = 0;
	struct drm_via_sg_info *vsg;
	int ret = -ENXIO;
	struct via_fence *fence;

	/* Real CPU physical address */
	old_start = (old_mem->start << PAGE_SHIFT) + old_mem->bus.base;
	new_start = (new_mem->start << PAGE_SHIFT) + new_mem->bus.base;

	if (old_mem->mem_type == TTM_PL_VRAM) {
		direction = DMA_FROM_DEVICE;
		dev_addr = old_start;
	} else if (new_mem->mem_type == TTM_PL_VRAM) {
		/* direction is DMA_TO_DEVICE */
		dev_addr = new_start;
	}

	/* device addr must be 16 byte align */
	if (dev_addr & 0x0F)
		return ret;

	vsg = via_alloc_desc_pages(bo->ttm, dev_priv->dev, dev_addr, direction);
	if (unlikely(IS_ERR(vsg)))
		return PTR_ERR(vsg);

	fence = via_fence_create_and_emit(dev_priv->dma_fences, vsg, 0);
	if (unlikely(IS_ERR(fence)))
		return PTR_ERR(fence);
	return ttm_bo_move_accel_cleanup(bo, (void *)fence, evict, no_wait_gpu, new_mem);
}

static int
via_move_from_vram(struct ttm_buffer_object *bo, bool interruptible,
			bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct ttm_mem_reg tmp_mem;
	struct ttm_placement placement;
	struct ttm_place place;
	int ret;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;

	place.fpfn = place.lpfn = 0;
	place.flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_TT;

	placement.num_busy_placement = placement.num_placement = 1;
	placement.busy_placement = placement.placement = &place;

	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem,
				interruptible, no_wait_gpu);
	if (unlikely(ret))
		return ret;

	/* Allocate some DMA memory for the GART address space */
	ret = ttm_tt_set_placement_caching(bo->ttm, tmp_mem.placement);
	if (unlikely(ret))
		goto out_cleanup;

	ret = ttm_tt_bind(bo->ttm, &tmp_mem);
	if (unlikely(ret))
		goto out_cleanup;

	/* Move from the VRAM to GART space */
	ret = via_move_blit(bo, true, no_wait_gpu, &tmp_mem, old_mem);
	if (unlikely(ret))
		goto out_cleanup;

	/* Expose the GART region to the system memory */
	ret = ttm_bo_move_ttm(bo, true, no_wait_gpu, new_mem);
out_cleanup:
	ttm_bo_mem_put(bo, &tmp_mem);
	return ret;
}

static int
via_move_to_vram(struct ttm_buffer_object *bo, bool interruptible,
			bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct ttm_mem_reg tmp_mem;
	struct ttm_placement placement;
	struct ttm_place place;
	int ret;

	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;

	place.fpfn = place.lpfn = 0;
	place.flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_TT;

	placement.busy_placement = placement.placement = &place;
	placement.num_busy_placement = placement.num_placement = 1;

	ret = ttm_bo_mem_space(bo, &placement, &tmp_mem,
				interruptible, no_wait_gpu);
	if (unlikely(ret))
		return ret;

	/* Expose the GART region to the system memory */
	ret = ttm_bo_move_ttm(bo, true, no_wait_gpu, &tmp_mem);
	if (unlikely(ret))
		goto out_cleanup;

	/* Move from the GART to VRAM */
	ret = via_move_blit(bo, true, no_wait_gpu, new_mem, old_mem);
out_cleanup:
	ttm_bo_mem_put(bo, &tmp_mem);
	return ret;
}

static int
via_bo_move(struct ttm_buffer_object *bo, bool evict, bool interruptible,
		bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
    struct ttm_mem_reg *old_mem = &bo->mem;
	int ret = 0;

	DRM_DEBUG("Entered via_bo_move.\n");

    if ((old_mem->mem_type == TTM_PL_SYSTEM) && (!bo->ttm)) {
        BUG_ON(old_mem->mm_node != NULL);
        *old_mem = *new_mem;
        new_mem->mm_node = NULL;
        goto exit;
    }

	/* No real memory copy. Just use the new_mem
	 * directly. */
	if (((old_mem->mem_type == TTM_PL_SYSTEM)
	        && (new_mem->mem_type == TTM_PL_TT))
        || ((old_mem->mem_type == TTM_PL_TT)
            && (new_mem->mem_type == TTM_PL_SYSTEM))
	    || (new_mem->mem_type == TTM_PL_PRIV0)) {
		BUG_ON(old_mem->mm_node != NULL);
		*old_mem = *new_mem;
		new_mem->mm_node = NULL;
        goto exit;
	}

	/* Accelerated copy involving the VRAM. */
	if ((old_mem->mem_type == TTM_PL_VRAM)
        && (new_mem->mem_type == TTM_PL_SYSTEM)) {
		ret = via_move_from_vram(bo, interruptible, no_wait_gpu, new_mem);
	} else if ((old_mem->mem_type == TTM_PL_SYSTEM)
        && (new_mem->mem_type == TTM_PL_VRAM)) {
		ret = via_move_to_vram(bo, interruptible, no_wait_gpu, new_mem);
	} else {
		ret = via_move_blit(bo, evict, no_wait_gpu, new_mem, old_mem);
	}

	if (ret) {
		ret = ttm_bo_move_memcpy(bo, evict, no_wait_gpu, new_mem);
	}

exit:
    DRM_DEBUG("Exiting via_bo_move.\n");
	return ret;
}

static int
via_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct via_device *dev_priv =
		container_of(bdev, struct via_device, bdev);
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct drm_device *dev = dev_priv->dev;

	mem->bus.base = 0;
	mem->bus.addr = NULL;
	mem->bus.offset = mem->start << PAGE_SHIFT;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.is_iomem = true;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		mem->bus.is_iomem = false;
		mem->bus.offset = 0;
		return 0;

	case TTM_PL_TT:
#if IS_ENABLED(CONFIG_AGP)
		if (drm_pci_device_is_agp(dev)) {
			mem->bus.is_iomem = !dev->agp->cant_use_aperture;
			mem->bus.base = dev->agp->base;
		}
#endif
		break;

	case TTM_PL_PRIV0:
		mem->bus.base = pci_resource_start(dev->pdev, 1);
		break;

	case TTM_PL_VRAM:
		if (dev->pdev->device == PCI_DEVICE_ID_VIA_VX900_VGA)
			mem->bus.base = pci_resource_start(dev->pdev, 2);
		else
			mem->bus.base = pci_resource_start(dev->pdev, 0);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static void via_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int via_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}

static struct ttm_bo_driver via_bo_driver = {
	.ttm_tt_create		= via_ttm_tt_create,
	.ttm_tt_populate	= via_ttm_tt_populate,
	.ttm_tt_unpopulate	= via_ttm_tt_unpopulate,
	.invalidate_caches	= via_invalidate_caches,
	.init_mem_type		= via_init_mem_type,
	.evict_flags		= via_evict_flags,
	.move			= via_bo_move,
	.verify_access		= via_verify_access,
	.io_mem_reserve		= via_ttm_io_mem_reserve,
	.io_mem_free		= via_ttm_io_mem_free,
};

int via_mm_init(struct via_device *dev_priv)
{
    struct drm_device *dev = dev_priv->dev;
    struct ttm_buffer_object *bo;
    unsigned long long start;
    int len;
    int ret;

    DRM_DEBUG("Entered via_mm_init.\n");

    ret = via_ttm_global_init(dev_priv);
	if (ret) {
        DRM_ERROR("Failed to initialise TTM: %d\n", ret);
        goto exit;
	}

	dev_priv->bdev.dev_mapping = dev->anon_inode->i_mapping;

    ret = ttm_bo_device_init(&dev_priv->bdev,
                                dev_priv->bo_global_ref.ref.object,
                                &via_bo_driver,
                                dev->anon_inode->i_mapping,
                                DRM_FILE_PAGE_OFFSET,
                                false);
    if (ret) {
        DRM_ERROR("Error initialising bo driver: %d\n", ret);
        goto exit;
    }

    ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_VRAM, dev_priv->vram_size >> PAGE_SHIFT);
    if (ret) {
        DRM_ERROR("Failed to map video RAM: %d\n", ret);
        goto exit;
    }

    /* Add an MTRR for the video RAM. */
    dev_priv->vram_mtrr = arch_phys_wc_add(dev_priv->vram_start, dev_priv->vram_size);

    DRM_INFO("Mapped %llu MB of video RAM at physical address 0x%08llx.\n",
                (unsigned long long) dev_priv->vram_size >> 20, dev_priv->vram_start);

    start = (unsigned long long) pci_resource_start(dev->pdev, 1);
    len = pci_resource_len(dev->pdev, 1);
    ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_PRIV0, len >> PAGE_SHIFT);
    if (ret) {
        DRM_ERROR("Failed to map MMIO: %d\n", ret);
        goto exit;
    }

    ret = via_bo_create(&dev_priv->bdev, VIA_MMIO_REGSIZE, ttm_bo_type_kernel,
                        TTM_PL_FLAG_PRIV0, 1, PAGE_SIZE, false, NULL, NULL, &bo);
    if (ret) {
        DRM_ERROR("Failed to create a buffer object for MMIO: %d\n", ret);
        goto exit;
    }

    ret = via_bo_pin(bo, &dev_priv->mmio);
    if (ret) {
        DRM_ERROR("Failed to map a buffer object for MMIO: %d\n", ret);
        ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_PRIV0);
        goto exit;
    }

    DRM_INFO("Mapped MMIO at physical address 0x%08llx.\n",
                start);
exit:
    DRM_DEBUG("Exiting via_mm_init.\n");
	return ret;
}

void via_mm_fini(struct drm_device *dev)
{
    struct via_device *dev_priv = dev->dev_private;

    DRM_DEBUG("Entered via_mm_fini.\n");

    ttm_bo_device_release(&dev_priv->bdev);

    via_ttm_global_release(&dev_priv->mem_global_ref,
            &dev_priv->bo_global_ref,
            &dev_priv->bdev);

    /* mtrr delete the vram */
    if (dev_priv->vram_mtrr >= 0) {
        arch_phys_wc_del(dev_priv->vram_mtrr);
    }

    dev_priv->vram_mtrr = 0;

    DRM_DEBUG("Exiting via_mm_fini.\n");
}

/*
 * the buffer object domain
 */
void
ttm_placement_from_domain(struct ttm_buffer_object *bo, struct ttm_placement *placement, u32 domains,
                struct ttm_bo_device *bdev)
{
    struct ttm_heap *heap = container_of(bo, struct ttm_heap, pbo);
    int cnt = 0, i = 0;

    if (!(domains & TTM_PL_MASK_MEM))
        domains = TTM_PL_FLAG_SYSTEM;

    do {
        int domain = (domains & (1 << i));

        if (domain) {
            heap->busy_placements[cnt].flags = (domain | bdev->man[i].default_caching);
            heap->busy_placements[cnt].fpfn = heap->busy_placements[cnt].lpfn = 0;
            heap->placements[cnt].flags = (domain | bdev->man[i].available_caching);
            heap->placements[cnt].fpfn = heap->placements[cnt].lpfn = 0;
            cnt++;
        }
    } while (i++ < TTM_NUM_MEM_TYPES);

    placement->num_busy_placement = placement->num_placement = cnt;
    placement->busy_placement = heap->busy_placements;
    placement->placement = heap->placements;
}

int
via_bo_create(struct ttm_bo_device *bdev,
        unsigned long size,
        enum ttm_bo_type origin,
        uint32_t domains,
        uint32_t byte_align,
        uint32_t page_align,
        bool interruptible,
        struct sg_table *sg,
        struct reservation_object *resv,
        struct ttm_buffer_object **p_bo)
{
    struct ttm_buffer_object *bo = NULL;
    struct ttm_placement placement;
    struct ttm_heap *heap;
    size_t acc_size;
    int ret = -ENOMEM;

    DRM_DEBUG("Entered via_bo_create.\n");

    size = round_up(size, byte_align);
    size = ALIGN(size, page_align);

    heap = kzalloc(sizeof(struct ttm_heap), GFP_KERNEL);
    if (unlikely(!heap)) {
        DRM_ERROR("Failed to allocate kernel memory.");
        goto exit;
    }

    bo = &heap->pbo;

    ttm_placement_from_domain(bo, &placement, domains, bdev);

    acc_size = ttm_bo_dma_acc_size(bdev, size,
                                    sizeof(struct ttm_heap));

    ret = ttm_bo_init(bdev, bo, size, origin, &placement,
              page_align >> PAGE_SHIFT,
              interruptible, NULL, acc_size,
              sg, NULL, via_ttm_bo_destroy);

    if (unlikely(ret)) {
        DRM_ERROR("Failed to initialize a TTM Buffer Object.");
        goto error;
    }

    *p_bo = bo;
    goto exit;
error:
    kfree(heap);
exit:
    DRM_DEBUG("Exiting via_bo_create.\n");
    return ret;
}

int
via_bo_pin(struct ttm_buffer_object *bo, struct ttm_bo_kmap_obj *kmap)
{
    struct ttm_heap *heap = container_of(bo, struct ttm_heap, pbo);
    struct ttm_placement placement;
    int ret;

    ret = ttm_bo_reserve(bo, true, false, false, 0);
    if (!ret) {
        placement.placement = heap->placements;
        placement.num_placement = 1;

        heap->placements[0].flags = (bo->mem.placement | TTM_PL_FLAG_NO_EVICT);
        ret = ttm_bo_validate(bo, &placement, false, false);
        if (!ret && kmap)
            ret = ttm_bo_kmap(bo, 0, bo->num_pages, kmap);
        ttm_bo_unreserve(bo);
    }
    return ret;
}

int
via_bo_unpin(struct ttm_buffer_object *bo, struct ttm_bo_kmap_obj *kmap)
{
    struct ttm_heap *heap = container_of(bo, struct ttm_heap, pbo);
    struct ttm_placement placement;
    int ret;

    ret = ttm_bo_reserve(bo, true, false, false, 0);
    if (!ret) {
        if (kmap)
            ttm_bo_kunmap(kmap);

        placement.placement = heap->placements;
        placement.num_placement = 1;

        heap->placements[0].flags = (bo->mem.placement & ~TTM_PL_FLAG_NO_EVICT);
        ret = ttm_bo_validate(bo, &placement, false, false);
        ttm_bo_unreserve(bo);
    }
    return ret;
}

int
via_ttm_allocate_kernel_buffer(struct ttm_bo_device *bdev, unsigned long size,
                uint32_t alignment, uint32_t domain,
                struct ttm_bo_kmap_obj *kmap)
{
    int ret = via_bo_create(bdev, size, ttm_bo_type_kernel, domain,
                  alignment, PAGE_SIZE, false, NULL,
                  NULL, &kmap->bo);
    if (likely(!ret)) {
        ret = via_bo_pin(kmap->bo, kmap);
        if (unlikely(ret)) {
            DRM_ERROR("failed to mmap the buffer\n");
            ttm_bo_unref(&kmap->bo);
            kmap->bo = NULL;
        }
    }
    return ret;
}
