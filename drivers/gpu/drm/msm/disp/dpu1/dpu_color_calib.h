/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DSPP colour calibration: a firmware blob with the IGC, 3D gamut and GC
 * tables of a panel (from the vendor QDCM calibration), and packers that
 * turn it into the streams the DSPP blocks take.
 */

#ifndef _DPU_COLOR_CALIB_H
#define _DPU_COLOR_CALIB_H

#include <linux/bits.h>
#include <linux/types.h>

#define DPU_CALIB_IGC_N		257
#define DPU_CALIB_GAMUT_DIM	17
#define DPU_CALIB_GAMUT_N	(DPU_CALIB_GAMUT_DIM * DPU_CALIB_GAMUT_DIM * DPU_CALIB_GAMUT_DIM)
#define DPU_CALIB_GC_N		1024

/* 258 entries x 3: 1548 bytes = 129 LUT-bus transfers of 12 bytes */
#define DPU_IGC_STREAM_U16	774
/* 4916 entries x 3: 29496 bytes = 1229 LUT-bus transfers of 24 bytes */
#define DPU_GAMUT_STREAM_U16	14748

#define DPU_GAMUT_LAYOUT_SWAP_RB_AXES	BIT(0)
/* bits [3:1]: which source channels feed (c0, c1, c2), see dpu_color_calib.c */
#define DPU_GAMUT_LAYOUT_PERM_SHIFT	1
/* permutation 4 (slots [R, G, B]), axes as stored: what milos takes (checked on the 4a) */
#define DPU_GAMUT_LAYOUT_DEFAULT	(4 << DPU_GAMUT_LAYOUT_PERM_SHIFT)

/* Channels are R, G, B; gamut entries are indexed r + 17g + 289b. */
struct dpu_color_calib {
	u16 igc[3][DPU_CALIB_IGC_N];		/* 12-bit */
	u16 gamut[DPU_CALIB_GAMUT_N][3];	/* 12-bit */
	u16 gc[3][DPU_CALIB_GC_N];		/* 10-bit */
};

int dpu_color_calib_parse(const u8 *data, size_t len, struct dpu_color_calib *out);
void dpu_color_calib_igc_stream(const struct dpu_color_calib *c, u16 *out);
void dpu_color_calib_gamut_stream(const struct dpu_color_calib *c, u32 layout, u16 *out);
int dpu_default_ctm_check(int cur, int val, bool running);
void dpu_color_calib_gc_words(const struct dpu_color_calib *c, u32 *c0, u32 *c1, u32 *c2);

#endif /* _DPU_COLOR_CALIB_H */
