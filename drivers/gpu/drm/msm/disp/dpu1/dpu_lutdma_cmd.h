/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Command buffer encoding for the MDSS LUTDMA (reg-DMA v3) engine.
 */

#ifndef _DPU_LUTDMA_CMD_H
#define _DPU_LUTDMA_CMD_H

#include <linux/bits.h>
#include <linux/types.h>

#define DPU_LUTDMA_DECODE_DSPP0	BIT(17)

#define DPU_LUTDMA_BLOCK_IGC	0
#define DPU_LUTDMA_BLOCK_GAMUT	1
#define DPU_LUTDMA_TRANS_IGC	3	/* dwords per LUT-bus transfer */
#define DPU_LUTDMA_TRANS_GAMUT	6

struct dpu_lutdma_cmdbuf {
	u32 *dw;
	u32 len;	/* dwords used */
	u32 cap;	/* dwords available */
};

void dpu_lutdma_cmd_init(struct dpu_lutdma_cmdbuf *b, u32 *mem, u32 cap_dw);
int dpu_lutdma_cmd_decode_sel(struct dpu_lutdma_cmdbuf *b, u32 mask);
int dpu_lutdma_cmd_write(struct dpu_lutdma_cmdbuf *b, u32 off, u32 val);
int dpu_lutdma_cmd_lut(struct dpu_lutdma_cmdbuf *b, u32 block_sel, u32 trans_size,
		       const u16 *data, u32 n_u16);
int dpu_lutdma_cmd_last(struct dpu_lutdma_cmdbuf *b);
u32 dpu_lutdma_cmd1(u32 len_dw, bool last);

#endif /* _DPU_LUTDMA_CMD_H */
