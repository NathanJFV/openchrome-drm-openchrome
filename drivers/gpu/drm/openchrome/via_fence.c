/*
 * Copyright 2013 James Simmons <jsimmons@infradead.org>
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
#include <linux/random.h>
#include "drmP.h"

#include "via_drv.h"

static void
via_fence_destroy(struct kref *kref)
{
	struct via_fence *fence = container_of(kref, struct via_fence, kref);

	if (fence->pool->fence_cleanup)
		fence->pool->fence_cleanup(fence);
	kfree(fence);
}

struct via_fence *
via_fence_create_and_emit(struct via_fence_pool *pool, void *data,
				unsigned int engine)
{
	struct via_fence *fence = NULL;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence) {
		unsigned long flags;
		int ret = -EINVAL;

		fence->timeout = jiffies + 3 * HZ;
		fence->engine = engine;
		fence->pool = pool;
		fence->priv = data;
		kref_init(&fence->kref);

		if (engine >= pool->num_engines) {
			via_fence_unref((void **) &fence);
			return ERR_PTR(-ENXIO);
		}
		spin_lock_irqsave(&pool->lock, flags);
try_again:
		/* I like to use get_random_init but it is not exported :-( */
		get_random_bytes(&fence->seq.key, 3);
		/* For the small change you get a zero */
		if (unlikely(fence->seq.key == 0))
			goto try_again;

		ret = drm_ht_insert_item_rcu(&pool->pending, &fence->seq);
		if (unlikely(ret))
			goto try_again;

		ret = pool->fence_emit(fence);
		if (ret) {
			DRM_INFO("Failed to emit fence\n");
			drm_ht_remove_item_rcu(&pool->pending, &fence->seq);
			via_fence_unref((void **) &fence);
			fence = ERR_PTR(ret);
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}
	return fence;
}

static void
via_fence_work(struct work_struct *work)
{
	struct via_fence_engine *eng = container_of(work, struct via_fence_engine,
							fence_work);
	unsigned long seq = readl(eng->read_seq), flags;
	struct via_fence_pool *pool = eng->pool;
	struct drm_hash_item *hash = NULL;
	int ret;

	spin_lock_irqsave(&eng->pool->lock, flags);
	ret = drm_ht_find_item_rcu(&pool->pending, seq, &hash);
	if (likely(ret == 0)) {
		ret = drm_ht_remove_item_rcu(&pool->pending, hash);
		if (ret < 0)
			DRM_DEBUG("Failed to remove seq %lx\n", seq);
	}
	if (eng->pool->fence_signaled)
		eng->pool->fence_signaled(eng);
	spin_unlock_irqrestore(&eng->pool->lock, flags);
}

static bool
via_fence_seq_signaled(struct via_fence *fence, u64 seq)
{
	struct drm_hash_item *key;
	unsigned long flags;
	bool ret = false;

	/* If the fence is no longer pending then its signaled */
	spin_lock_irqsave(&fence->pool->lock, flags);
	if (drm_ht_find_item_rcu(&fence->pool->pending, seq, &key))
		ret = true;
	spin_unlock_irqrestore(&fence->pool->lock, flags);
	return ret;
}

/* TTM fence methods */
bool
via_fence_signaled(void *sync_obj)
{
	struct via_fence *fence = sync_obj;

	if (!fence || !fence->seq.key)
		return true;

	if (via_fence_seq_signaled(fence, fence->seq.key)) {
		fence->seq.key = 0;
		return true;
	}
	return false;
}

int
via_fence_wait(void *sync_obj, bool lazy, bool interruptible)
{
	struct via_fence *fence = sync_obj;
	int ret = 0;

	while (!via_fence_seq_signaled(fence, fence->seq.key)) {
		if (time_after(jiffies, fence->timeout)) {
			DRM_INFO("The fence wait timeout timeout = %lu, jiffies = %lu.\n",
				fence->timeout, jiffies);
			ret = -EBUSY;
			break;
		}

		set_current_state(interruptible ? TASK_INTERRUPTIBLE :
						TASK_UNINTERRUPTIBLE);

		if (interruptible && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	return ret;
}

int
via_fence_flush(void *sync_obj)
{
	return 0;
}

void
via_fence_unref(void **sync_obj)
{
	struct via_fence *fence = *sync_obj;

	*sync_obj = NULL;
	if (fence)
		kref_put(&fence->kref, &via_fence_destroy);
}

void *
via_fence_ref(void *sync_obj)
{
	struct via_fence *fence = sync_obj;

	kref_get(&fence->kref);
	return sync_obj;
}

/* We assert 30 * sizeof(uint32_t) is enough for emit fence sequence */
#define FENCE_CMD_BUFFER (256 * sizeof(uint32_t))

struct via_fence_pool *
via_fence_pool_init(struct drm_device *dev, char *name, int domain,
			int num_engines)
{
	struct via_device *dev_priv = dev->dev_private;
	struct via_fence_pool *pool = NULL;
	int ret = 0, size, i;
	void *par = NULL;

	size = sizeof(*pool) + num_engines * sizeof(*pool->engines);
	pool = kzalloc(size, GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	/* allocate fence sync bo */
	ret = via_ttm_allocate_kernel_buffer(&dev_priv->ttm.bdev, PAGE_SIZE, 16,
					domain, &pool->fence_sync);
	if (unlikely(ret)) {
		DRM_ERROR("allocate fence sync bo error.\n");
		goto out_err;
	}
	ret = -ENOMEM;

	pool->cmd_buffer = kzalloc(FENCE_CMD_BUFFER, GFP_KERNEL);
	if (!pool->cmd_buffer)
		goto out_err;

	spin_lock_init(&pool->lock);
	pool->num_engines = num_engines;
	pool->dev = dev;

	if (domain == TTM_PL_FLAG_TT) {
		pool->bus_addr = dma_map_page(dev->dev, pool->fence_sync.bo->ttm->pages[0],
						0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		par = pool->fence_sync.virtual;
	} else if (domain == TTM_PL_FLAG_VRAM) {
		pool->bus_addr = dma_map_single(dev->dev, pool->cmd_buffer,
						FENCE_CMD_BUFFER, DMA_TO_DEVICE);
		par = pool->cmd_buffer;
	}

	for (i = 0; i < pool->num_engines; i++) {
		struct via_fence_engine *eng = &pool->engines[i];

		INIT_WORK(&eng->fence_work, via_fence_work);
		eng->read_seq = par + VIA_FENCE_SIZE * i;
		eng->pool = pool;
		eng->index = i;
	}

	pool->fence_wq = alloc_workqueue(name, 0, 0);
	if (!pool->fence_wq)
		goto out_err;

	ret = drm_ht_create(&pool->pending, 12);
out_err:
	if (ret) {
		via_fence_pool_fini(pool);
		pool = ERR_PTR(ret);
	}
	return pool;
}

void
via_fence_pool_fini(struct via_fence_pool *pool)
{
	struct ttm_buffer_object *sync_bo;
	int i;

	if (!pool)
		return;

	drm_ht_remove(&pool->pending);

	flush_workqueue(pool->fence_wq);
	destroy_workqueue(pool->fence_wq);

	for (i = 0; i < pool->num_engines; i++)
		cancel_work_sync(&pool->engines[i].fence_work);

	kfree(pool->cmd_buffer);

	sync_bo = pool->fence_sync.bo;
	if (sync_bo) {
		via_bo_unpin(sync_bo, &pool->fence_sync);
		ttm_bo_unref(&sync_bo);
	}

	if (pool->bus_addr)
		dma_unmap_page(pool->dev->dev, pool->bus_addr, PAGE_SIZE,
				DMA_BIDIRECTIONAL);
	kfree(pool);
}
