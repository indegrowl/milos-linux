/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MDSS LUTDMA (reg-DMA v3) engine: runs command buffers that load DSPP LUTs.
 */

#ifndef _DPU_HW_LUTDMA_H
#define _DPU_HW_LUTDMA_H

#include <linux/types.h>

#include "dpu_lutdma_cmd.h"

struct drm_device;
struct drm_gpuvm;
struct dpu_hw_ctl;
struct dpu_hw_lutdma;

struct dpu_hw_lutdma *dpu_hw_lutdma_init(struct drm_device *dev, struct drm_gpuvm *vm,
					 void __iomem *base);
void dpu_hw_lutdma_destroy(struct dpu_hw_lutdma *dma);
struct dpu_lutdma_cmdbuf *dpu_hw_lutdma_begin(struct dpu_hw_lutdma *dma);
int dpu_hw_lutdma_submit(struct dpu_hw_lutdma *dma, struct dpu_hw_ctl *ctl);
u32 dpu_hw_lutdma_error_status(struct dpu_hw_lutdma *dma);

#endif /* _DPU_HW_LUTDMA_H */
