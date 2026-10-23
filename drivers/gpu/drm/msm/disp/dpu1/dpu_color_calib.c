// SPDX-License-Identifier: GPL-2.0-only
/*
 * DSPP colour calibration blob parser and LUT stream packers.
 *
 * Blob v1, little-endian: "DSPC", u32 version (1), u32 igc_n (257),
 * u32 gamut_dim (17), u32 gc_n (1024), u16 igc[3][257] (R, G, B; 12-bit),
 * u16 gamut[4913][3] ([R, G, B] 12-bit, index r + 17g + 289b),
 * u16 gc[3][1024] (R, G, B; 10-bit), u32 crc32 (zlib) of everything before it.
 */

#include <linux/crc32.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "dpu_color_calib.h"

#define CALIB_HDR	20
#define CALIB_SIZE	(CALIB_HDR + 2 * (3 * DPU_CALIB_IGC_N + 3 * DPU_CALIB_GAMUT_N + \
				       3 * DPU_CALIB_GC_N) + 4)

enum { R, G, B };

/* Source channels for the DSPP's (c0, c1, c2), by layout permutation index */
static const u8 dpu_gamut_perm[6][3] = {
	{ G, B, R }, { R, G, B }, { B, R, G }, { G, R, B }, { R, B, G }, { B, G, R },
};

static int read_table(const u8 **p, u16 *dst, int n, u16 max)
{
	int i;

	for (i = 0; i < n; i++) {
		dst[i] = get_unaligned_le16(*p);
		*p += 2;
		if (dst[i] > max)
			return -EINVAL;
	}
	return 0;
}

int dpu_color_calib_parse(const u8 *data, size_t len, struct dpu_color_calib *out)
{
	const u8 *p = data + CALIB_HDR;
	int ch, ret = 0;

	if (len != CALIB_SIZE || memcmp(data, "DSPC", 4) ||
	    get_unaligned_le32(data + 4) != 1 ||
	    get_unaligned_le32(data + 8) != DPU_CALIB_IGC_N ||
	    get_unaligned_le32(data + 12) != DPU_CALIB_GAMUT_DIM ||
	    get_unaligned_le32(data + 16) != DPU_CALIB_GC_N)
		return -EINVAL;

	if ((crc32_le(~0, data, len - 4) ^ ~0) != get_unaligned_le32(data + len - 4))
		return -EBADMSG;

	for (ch = 0; ch < 3 && !ret; ch++)
		ret = read_table(&p, out->igc[ch], DPU_CALIB_IGC_N, 4095);
	if (!ret)
		ret = read_table(&p, &out->gamut[0][0], 3 * DPU_CALIB_GAMUT_N, 4095);
	for (ch = 0; ch < 3 && !ret; ch++)
		ret = read_table(&p, out->gc[ch], DPU_CALIB_GC_N, 1023);

	return ret;
}

/* 257 [R, G, B] entries, MSB-aligned; the last entry is forced to full scale */
void dpu_color_calib_igc_stream(const struct dpu_color_calib *c, u16 *out)
{
	int i, ch;

	for (i = 0; i < DPU_CALIB_IGC_N - 1; i++)
		for (ch = 0; ch < 3; ch++)
			out[3 * i + ch] = c->igc[ch][i] << 4;
	for (ch = 0; ch < 3; ch++)
		out[3 * i + ch] = 4095 << 4;
	memset(&out[3 * DPU_CALIB_IGC_N], 0,
	       (DPU_IGC_STREAM_U16 - 3 * DPU_CALIB_IGC_N) * sizeof(*out));
}

/*
 * The linear 17^3 cube in order, each entry as [c0, c2, c1] MSB-aligned,
 * then zero padding to a whole number of transfers.
 */
void dpu_color_calib_gamut_stream(const struct dpu_color_calib *c, u32 layout, u16 *out)
{
	const int d = DPU_CALIB_GAMUT_DIM;
	u32 perm = layout >> DPU_GAMUT_LAYOUT_PERM_SHIFT;
	const u8 *src_ch = dpu_gamut_perm[perm < 6 ? perm : 0];
	int n;

	for (n = 0; n < DPU_CALIB_GAMUT_N; n++) {
		int src = n;
		const u16 *e;

		if (layout & DPU_GAMUT_LAYOUT_SWAP_RB_AXES)
			src = n / (d * d) + (n / d % d) * d + (n % d) * d * d;
		e = c->gamut[src];
		out[3 * n] = e[src_ch[0]] << 4;
		out[3 * n + 1] = e[src_ch[2]] << 4;
		out[3 * n + 2] = e[src_ch[1]] << 4;
	}
	memset(&out[3 * DPU_CALIB_GAMUT_N], 0,
	       (DPU_GAMUT_STREAM_U16 - 3 * DPU_CALIB_GAMUT_N) * sizeof(*out));
}

/* GC: two 10-bit entries per word; c0 = G, c1 = B, c2 = R */
void dpu_color_calib_gc_words(const struct dpu_color_calib *c, u32 *c0, u32 *c1, u32 *c2)
{
	u32 *dst[3] = { c2, c0, c1 };	/* indexed by R, G, B */
	int i, ch;

	for (ch = 0; ch < 3; ch++)
		for (i = 0; i < DPU_CALIB_GC_N / 2; i++)
			dst[ch][i] = (c->gc[ch][2 * i] & 0x3ff) |
				     (c->gc[ch][2 * i + 1] & 0x3ff) << 16;
}
