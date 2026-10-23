// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_managed.h>

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_dspp.h"
#include "dpu_kms.h"
#include "dpu_color_calib.h"
#include "dpu_lutdma_cmd.h"


/* DSPP_PCC */
#define PCC_EN BIT(0)
#define PCC_DIS 0
#define PCC_RED_R_OFF 0x10
#define PCC_RED_G_OFF 0x1C
#define PCC_RED_B_OFF 0x28
#define PCC_GREEN_R_OFF 0x14
#define PCC_GREEN_G_OFF 0x20
#define PCC_GREEN_B_OFF 0x2C
#define PCC_BLUE_R_OFF 0x18
#define PCC_BLUE_G_OFF 0x24
#define PCC_BLUE_B_OFF 0x30

/* DSPP_GC */
#define GC_EN BIT(0)
#define GC_DIS 0
#define GC_8B_ROUND_EN BIT(1)
#define GC_LUT_SWAP_OFF 0x1c
#define GC_C0_OFF 0x4
#define GC_C1_OFF 0xc
#define GC_C2_OFF 0x14
#define GC_C0_INDEX_OFF 0x8
#define GC_C1_INDEX_OFF 0x10
#define GC_C2_INDEX_OFF 0x18

static void dpu_setup_dspp_pcc(struct dpu_hw_dspp *ctx,
		struct dpu_hw_pcc_cfg *cfg)
{

	u32 base;

	if (!ctx) {
		DRM_ERROR("invalid ctx %p\n", ctx);
		return;
	}

	base = ctx->cap->sblk->pcc.base;

	if (!base) {
		DRM_ERROR("invalid ctx %p pcc base 0x%x\n", ctx, base);
		return;
	}

	if (!cfg) {
		DRM_DEBUG_DRIVER("disable pcc feature\n");
		DPU_REG_WRITE(&ctx->hw, base, PCC_DIS);
		return;
	}

	DPU_REG_WRITE(&ctx->hw, base + PCC_RED_R_OFF, cfg->r.r);
	DPU_REG_WRITE(&ctx->hw, base + PCC_RED_G_OFF, cfg->r.g);
	DPU_REG_WRITE(&ctx->hw, base + PCC_RED_B_OFF, cfg->r.b);

	DPU_REG_WRITE(&ctx->hw, base + PCC_GREEN_R_OFF, cfg->g.r);
	DPU_REG_WRITE(&ctx->hw, base + PCC_GREEN_G_OFF, cfg->g.g);
	DPU_REG_WRITE(&ctx->hw, base + PCC_GREEN_B_OFF, cfg->g.b);

	DPU_REG_WRITE(&ctx->hw, base + PCC_BLUE_R_OFF, cfg->b.r);
	DPU_REG_WRITE(&ctx->hw, base + PCC_BLUE_G_OFF, cfg->b.g);
	DPU_REG_WRITE(&ctx->hw, base + PCC_BLUE_B_OFF, cfg->b.b);

	DPU_REG_WRITE(&ctx->hw, base, PCC_EN);
}

static void dpu_setup_dspp_gc(struct dpu_hw_dspp *ctx,
		struct dpu_hw_gc_lut *gc_lut)
{
	int i = 0;
	u32 base, reg;

	if (!ctx) {
		DRM_ERROR("invalid ctx\n");
		return;
	}

	base = ctx->cap->sblk->gc.base;

	if (!base) {
		DRM_ERROR("invalid ctx %p gc base\n", ctx);
		return;
	}

	if (!gc_lut) {
		DRM_DEBUG_DRIVER("disable gc feature\n");
		DPU_REG_WRITE(&ctx->hw, base, GC_DIS);
		return;
	}

	DPU_REG_WRITE(&ctx->hw, base + GC_C0_INDEX_OFF, 0);
	DPU_REG_WRITE(&ctx->hw, base + GC_C1_INDEX_OFF, 0);
	DPU_REG_WRITE(&ctx->hw, base + GC_C2_INDEX_OFF, 0);

	for (i = 0; i < PGC_TBL_LEN; i++) {
		DPU_REG_WRITE(&ctx->hw, base + GC_C0_OFF, gc_lut->c0[i]);
		DPU_REG_WRITE(&ctx->hw, base + GC_C1_OFF, gc_lut->c1[i]);
		DPU_REG_WRITE(&ctx->hw, base + GC_C2_OFF, gc_lut->c2[i]);
	}

	DPU_REG_WRITE(&ctx->hw, base + GC_LUT_SWAP_OFF, BIT(0));

	reg = GC_EN | ((gc_lut->flags & PGC_8B_ROUND) ? GC_8B_ROUND_EN : 0);
	DPU_REG_WRITE(&ctx->hw, base, reg);
}

#define IGC_OPMODE_OFF		0x4
#define IGC_EN			BIT(8)
#define GAMUT_EN		BIT(0)	/* 17x17x17 mode, no scale/offset map */

static int dpu_build_dspp_lut(struct dpu_hw_dspp *ctx, struct dpu_lutdma_cmdbuf *b,
			      u32 block, u32 trans, const u16 *stream, u32 n_u16,
			      u32 opmode_off, u32 enable)
{
	int ret;

	ret = dpu_lutdma_cmd_decode_sel(b, DPU_LUTDMA_DECODE_DSPP0 << (ctx->idx - DSPP_0));
	if (!ret && stream)
		ret = dpu_lutdma_cmd_lut(b, block, trans, stream, n_u16);
	if (!ret)
		ret = dpu_lutdma_cmd_write(b, opmode_off, stream ? enable : 0);
	return ret;
}

static int dpu_build_dspp_igc(struct dpu_hw_dspp *ctx, struct dpu_lutdma_cmdbuf *b,
			      const u16 *stream)
{
	return dpu_build_dspp_lut(ctx, b, DPU_LUTDMA_BLOCK_IGC, DPU_LUTDMA_TRANS_IGC,
				  stream, DPU_IGC_STREAM_U16,
				  ctx->cap->sblk->igc.base + IGC_OPMODE_OFF, IGC_EN);
}

static int dpu_build_dspp_gamut(struct dpu_hw_dspp *ctx, struct dpu_lutdma_cmdbuf *b,
				const u16 *stream)
{
	return dpu_build_dspp_lut(ctx, b, DPU_LUTDMA_BLOCK_GAMUT, DPU_LUTDMA_TRANS_GAMUT,
				  stream, DPU_GAMUT_STREAM_U16,
				  ctx->cap->sblk->gamut.base, GAMUT_EN);
}

static void dpu_disable_dspp_luts(struct dpu_hw_dspp *ctx)
{
	DPU_REG_WRITE(&ctx->hw, ctx->cap->sblk->igc.base + IGC_OPMODE_OFF, 0);
	DPU_REG_WRITE(&ctx->hw, ctx->cap->sblk->gamut.base, 0);
}

/**
 * dpu_hw_dspp_init() - Initializes the DSPP hw driver object.
 * should be called once before accessing every DSPP.
 * @dev:  Corresponding device for devres management
 * @cfg:  DSPP catalog entry for which driver object is required
 * @addr: Mapped register io address of MDP
 * Return: pointer to structure or ERR_PTR
 */
struct dpu_hw_dspp *dpu_hw_dspp_init(struct drm_device *dev,
				     const struct dpu_dspp_cfg *cfg,
				     void __iomem *addr)
{
	struct dpu_hw_dspp *c;

	if (!addr)
		return ERR_PTR(-EINVAL);

	c = drmm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_DSPP;

	/* Assign ops */
	c->idx = cfg->id;
	c->cap = cfg;
	if (c->cap->sblk->pcc.base)
		c->ops.setup_pcc = dpu_setup_dspp_pcc;
	if (c->cap->sblk->gc.base)
		c->ops.setup_gc = dpu_setup_dspp_gc;
	if (c->cap->sblk->igc.base)
		c->ops.build_igc = dpu_build_dspp_igc;
	if (c->cap->sblk->gamut.base)
		c->ops.build_gamut = dpu_build_dspp_gamut;
	if (c->cap->sblk->igc.base && c->cap->sblk->gamut.base)
		c->ops.disable_luts = dpu_disable_dspp_luts;

	return c;
}
