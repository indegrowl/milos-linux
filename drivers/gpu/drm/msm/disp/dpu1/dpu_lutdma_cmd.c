// SPDX-License-Identifier: GPL-2.0-only
/*
 * Command buffer encoding for the MDSS LUTDMA (reg-DMA v3) engine.
 *
 * Every operation is a header dword (opcode | block-relative offset) followed
 * by its data. A buffer starts with a decode select, which picks the block the
 * following offsets are relative to.
 */

#include <linux/errno.h>
#include <linux/string.h>

#include "dpu_lutdma_cmd.h"

#define LUTDMA_DECODE_SEL	0x18180114	/* absolute write to regdma+0x114 */
#define LUTDMA_SINGLE_WRITE	BIT(28)
#define LUTDMA_LUT_WRITE	(BIT(30) | BIT(29))
#define LUTDMA_OFF_MASK		0x1fffff
#define LUTDMA_LUT_NOPS		3	/* hardware needs no-ops after a LUT-bus write */

#define LUTDMA_CMD1_WRITE	BIT(22)
#define LUTDMA_CMD1_LAST	BIT(24)
#define LUTDMA_CMD1_LEN_MASK	0x3fff

void dpu_lutdma_cmd_init(struct dpu_lutdma_cmdbuf *b, u32 *mem, u32 cap_dw)
{
	b->dw = mem;
	b->len = 0;
	b->cap = cap_dw;
}

static int put2(struct dpu_lutdma_cmdbuf *b, u32 a, u32 v)
{
	if (b->cap - b->len < 2)
		return -ENOSPC;
	b->dw[b->len++] = a;
	b->dw[b->len++] = v;
	return 0;
}

int dpu_lutdma_cmd_decode_sel(struct dpu_lutdma_cmdbuf *b, u32 mask)
{
	return put2(b, LUTDMA_DECODE_SEL, mask);
}

int dpu_lutdma_cmd_write(struct dpu_lutdma_cmdbuf *b, u32 off, u32 val)
{
	if (off > LUTDMA_OFF_MASK)
		return -EINVAL;
	return put2(b, LUTDMA_SINGLE_WRITE | off, val);
}

int dpu_lutdma_cmd_lut(struct dpu_lutdma_cmdbuf *b, u32 block_sel, u32 trans_size,
		       const u16 *data, u32 n_u16)
{
	u32 bytes = n_u16 * 2, xfer = trans_size * 4, words = bytes / 4, i;

	if (!xfer || bytes % xfer)
		return -EINVAL;
	if (b->cap - b->len < 2 + words + LUTDMA_LUT_NOPS)
		return -ENOSPC;

	b->dw[b->len++] = LUTDMA_LUT_WRITE | block_sel;
	b->dw[b->len++] = trans_size << 16 | bytes / xfer;
	for (i = 0; i < words; i++)
		b->dw[b->len++] = data[2 * i] | (u32)data[2 * i + 1] << 16;
	for (i = 0; i < LUTDMA_LUT_NOPS; i++)
		b->dw[b->len++] = 0;
	return 0;
}

/* The tiny buffer queued after the real ones, with the last-command flag */
int dpu_lutdma_cmd_last(struct dpu_lutdma_cmdbuf *b)
{
	return put2(b, LUTDMA_DECODE_SEL, 0);
}

u32 dpu_lutdma_cmd1(u32 len_dw, bool last)
{
	return LUTDMA_CMD1_WRITE | (last ? LUTDMA_CMD1_LAST : 0) | (len_dw & LUTDMA_CMD1_LEN_MASK);
}
