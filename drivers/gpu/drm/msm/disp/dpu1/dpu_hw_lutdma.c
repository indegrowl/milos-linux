// SPDX-License-Identifier: GPL-2.0-only
/*
 * MDSS LUTDMA (reg-DMA v3) engine, double-buffered (DB) engine only.
 *
 * A command buffer is queued on the CTL's queue 0 together with a tiny
 * last-command buffer, started with the CTL's software trigger and polled
 * until done. The LUT RAM is written right away, not at a frame boundary.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "msm_drv.h"
#include "msm_gem.h"

#include "dpu_hw_ctl.h"
#include "dpu_hw_lutdma.h"
#include "dpu_kms.h"

#define LUTDMA_OPMODE		0x4
#define LUTDMA_ERR_STATUS	0x170
#define LUTDMA_ERR_CLEAR	0x1b0
#define LUTDMA_ERR_CLEAR_MASK	0xf0007
#define LUTDMA_CLK_FORCE	0x1f0
#define LUTDMA_CLK_FORCE_ON	(BIT(0) | BIT(16))
/* per-CTL block, indexed by the CTL enum (CTL_0 = 1): the engine-wide registers sit below 0x1000 */
#define LUTDMA_CTL(ctl)		(0x1000 * (ctl))
#define LUTDMA_Q0_ADDR		0x0
#define LUTDMA_Q0_CMD		0x4
#define LUTDMA_DONE		0x44
#define LUTDMA_DONE_CLEAR	0x48
#define LUTDMA_DONE_Q0		BIT(3)

#define LUTDMA_BO_SIZE		SZ_64K
#define LUTDMA_LAST_OFF		61440	/* last-command buffer, 256-byte aligned */
#define LUTDMA_CMD_CAP		(LUTDMA_LAST_OFF / 4)
#define LUTDMA_TIMEOUT_US	20000

struct dpu_hw_lutdma {
	void __iomem *base;
	struct drm_gpuvm *vm;
	struct drm_gem_object *bo;
	u32 *vaddr;
	u64 iova;
	struct dpu_lutdma_cmdbuf cmd;
	u32 err_status;
};

struct dpu_hw_lutdma *dpu_hw_lutdma_init(struct drm_device *dev, struct drm_gpuvm *vm,
					 void __iomem *base)
{
	struct dpu_hw_lutdma *dma;
	struct dpu_lutdma_cmdbuf last;

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return ERR_PTR(-ENOMEM);

	dma->base = base;
	dma->vm = vm;
	dma->vaddr = msm_gem_kernel_new(dev, LUTDMA_BO_SIZE, MSM_BO_WC, vm, &dma->bo, &dma->iova);
	if (IS_ERR(dma->vaddr)) {
		int ret = PTR_ERR(dma->vaddr);

		kfree(dma);
		return ERR_PTR(ret);
	}
	msm_gem_object_set_name(dma->bo, "lutdma");

	/* The queue address registers are 32 bits wide */
	if (WARN_ON(upper_32_bits(dma->iova + LUTDMA_BO_SIZE - 1))) {
		dpu_hw_lutdma_destroy(dma);
		return ERR_PTR(-ERANGE);
	}

	dpu_lutdma_cmd_init(&last, dma->vaddr + LUTDMA_LAST_OFF / 4, 2);
	dpu_lutdma_cmd_last(&last);

	drm_info(dev, "lutdma: buffer at iova 0x%llx\n", dma->iova);
	return dma;
}

void dpu_hw_lutdma_destroy(struct dpu_hw_lutdma *dma)
{
	if (IS_ERR_OR_NULL(dma))
		return;
	msm_gem_kernel_put(dma->bo, dma->vm);
	kfree(dma);
}

struct dpu_lutdma_cmdbuf *dpu_hw_lutdma_begin(struct dpu_hw_lutdma *dma)
{
	dpu_lutdma_cmd_init(&dma->cmd, dma->vaddr, LUTDMA_CMD_CAP);
	return &dma->cmd;
}

u32 dpu_hw_lutdma_error_status(struct dpu_hw_lutdma *dma)
{
	return dma->err_status;
}

int dpu_hw_lutdma_submit(struct dpu_hw_lutdma *dma, struct dpu_hw_ctl *ctl)
{
	void __iomem *q = dma->base + LUTDMA_CTL(ctl->idx);
	u32 done;
	int ret;

	if (!dma->cmd.len)
		return -EINVAL;

	/* the buffer is write-combined: make it visible before the engine reads it */
	wmb();

	writel(readl(dma->base + LUTDMA_CLK_FORCE) | LUTDMA_CLK_FORCE_ON,
	       dma->base + LUTDMA_CLK_FORCE);
	writel(1, dma->base + LUTDMA_OPMODE);
	if (readl(dma->base + LUTDMA_ERR_STATUS))
		writel(LUTDMA_ERR_CLEAR_MASK, dma->base + LUTDMA_ERR_CLEAR);
	writel(LUTDMA_DONE_Q0, q + LUTDMA_DONE_CLEAR);

	writel(lower_32_bits(dma->iova), q + LUTDMA_Q0_ADDR);
	writel(dpu_lutdma_cmd1(dma->cmd.len, false), q + LUTDMA_Q0_CMD);
	writel(lower_32_bits(dma->iova + LUTDMA_LAST_OFF), q + LUTDMA_Q0_ADDR);
	writel(dpu_lutdma_cmd1(2, true), q + LUTDMA_Q0_CMD);

	ctl->ops.trigger_lutdma(ctl);

	ret = readl_poll_timeout(q + LUTDMA_DONE, done, done & LUTDMA_DONE_Q0,
				 10, LUTDMA_TIMEOUT_US);
	dma->err_status = readl(dma->base + LUTDMA_ERR_STATUS);

	writel(readl(dma->base + LUTDMA_CLK_FORCE) & ~LUTDMA_CLK_FORCE_ON,
	       dma->base + LUTDMA_CLK_FORCE);

	if (ret)
		return -ETIMEDOUT;
	return dma->err_status ? -EIO : 0;
}
