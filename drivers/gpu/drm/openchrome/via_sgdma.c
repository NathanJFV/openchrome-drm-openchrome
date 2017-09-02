/**************************************************************************
 *
 * Copyright (c) 2012 James Simmons <jsimmons@infradead.org>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "via_drv.h"

static int
via_pcie_sgdma_bind(struct ttm_tt *ttm, struct ttm_mem_reg *mem)
{
	struct sgdma_tt *dma_tt = (struct sgdma_tt *) ttm;
	struct via_device *dev_priv =
		container_of(ttm->bdev, struct via_device, ttm.bdev);
	int i;

	/* Disable gart table HW protect */
	svga_wseq_mask(VGABASE, 0x6C, 0x00, BIT(7));

	/* Update the relevant entries */
	dma_tt->offset = mem->start << PAGE_SHIFT;
	for (i = 0; i < ttm->num_pages; i++) {
		writel(page_to_pfn(ttm->pages[i]) & 0x3FFFFFFF,
			dev_priv->gart.virtual + dma_tt->offset + i);
	}

	/* Invalided GTI cache */
	svga_wseq_mask(VGABASE, 0x6F, BIT(7), BIT(7));

	/* Enable gart table HW protect */
	svga_wseq_mask(VGABASE, 0x6C, BIT(7), BIT(7));
	return 1;
}

static int
via_pcie_sgdma_unbind(struct ttm_tt *ttm)
{
	struct sgdma_tt *dma_tt = (struct sgdma_tt *) ttm;
	struct via_device *dev_priv =
		container_of(ttm->bdev, struct via_device, ttm.bdev);
	int i;

	if (ttm->state != tt_bound)
		return 0;

	/* Disable gart table HW protect */
	svga_wseq_mask(VGABASE, 0x6C, 0x00, BIT(7));

	/* Update the relevant entries */
	for (i = 0; i < ttm->num_pages; i++)
		writel(0x80000000, dev_priv->gart.virtual + dma_tt->offset + i);
	dma_tt->offset = 0;

	/* Invalided GTI cache */
	svga_wseq_mask(VGABASE, 0x6F, BIT(7), BIT(7));

	/* Enable gart table HW protect */
	svga_wseq_mask(VGABASE, 0x6C, BIT(7), BIT(7));
	return 0;
}

static void
via_sgdma_destroy(struct ttm_tt *ttm)
{
	struct sgdma_tt *dma_tt = (struct sgdma_tt *) ttm;

	if (ttm) {
		ttm_dma_tt_fini(&dma_tt->sgdma);
		kfree(dma_tt);
	}
}

static struct ttm_backend_func ttm_sgdma_func = {
	.bind = via_pcie_sgdma_bind,
	.unbind = via_pcie_sgdma_unbind,
	.destroy = via_sgdma_destroy,
};

struct ttm_tt *
via_sgdma_backend_init(struct ttm_bo_device *bdev, unsigned long size,
			uint32_t page_flags, struct page *dummy_read_page)
{
	struct sgdma_tt *dma_tt;

	dma_tt = kzalloc(sizeof(*dma_tt), GFP_KERNEL);
	if (!dma_tt)
		return NULL;

	dma_tt->sgdma.ttm.func = &ttm_sgdma_func;

	if (ttm_dma_tt_init(&dma_tt->sgdma, bdev, size, page_flags, dummy_read_page)) {
		kfree(dma_tt);
		return NULL;
	}
	return &dma_tt->sgdma.ttm;
}
EXPORT_SYMBOL(via_sgdma_backend_init);
