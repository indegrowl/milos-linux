// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the DSPP colour calibration helpers. The helpers have no
 * hardware dependencies, so they are built into the test directly.
 */

#include <kunit/test.h>
#include <linux/crc32.h>
#include <linux/slab.h>

#include "dpu_color_calib.c"

#define BLOB_IGC	20
#define BLOB_GAMUT	1562
#define BLOB_GC		31040
#define BLOB_CRC	37184
#define BLOB_SIZE	37188

static void put16(u8 *b, size_t off, u16 v)
{
	b[off] = v & 0xff;
	b[off + 1] = v >> 8;
}

static void put32(u8 *b, size_t off, u32 v)
{
	put16(b, off, v & 0xffff);
	put16(b, off + 2, v >> 16);
}

static u16 ramp_igc(int ch, int i) { return i * 15 + ch; }
static u16 ramp_gamut(int n, int c) { return (n * 3 + c) % 4096; }
static u16 ramp_gc(int ch, int i) { return (i + ch * 7) % 1024; }

static void blob_crc(u8 *b)
{
	put32(b, BLOB_CRC, crc32_le(~0, b, BLOB_CRC) ^ ~0);
}

/* A valid blob filled with ramps */
static u8 *make_blob(struct kunit *test)
{
	u8 *b = kunit_kzalloc(test, BLOB_SIZE, GFP_KERNEL);
	int i, ch, n, c;

	KUNIT_ASSERT_NOT_NULL(test, b);
	memcpy(b, "DSPC", 4);
	put32(b, 4, 1);
	put32(b, 8, DPU_CALIB_IGC_N);
	put32(b, 12, DPU_CALIB_GAMUT_DIM);
	put32(b, 16, DPU_CALIB_GC_N);
	for (ch = 0; ch < 3; ch++)
		for (i = 0; i < DPU_CALIB_IGC_N; i++)
			put16(b, BLOB_IGC + 2 * (ch * DPU_CALIB_IGC_N + i), ramp_igc(ch, i));
	for (n = 0; n < DPU_CALIB_GAMUT_N; n++)
		for (c = 0; c < 3; c++)
			put16(b, BLOB_GAMUT + 2 * (n * 3 + c), ramp_gamut(n, c));
	for (ch = 0; ch < 3; ch++)
		for (i = 0; i < DPU_CALIB_GC_N; i++)
			put16(b, BLOB_GC + 2 * (ch * DPU_CALIB_GC_N + i), ramp_gc(ch, i));
	blob_crc(b);
	return b;
}

static struct dpu_color_calib *parsed(struct kunit *test)
{
	struct dpu_color_calib *c = kunit_kzalloc(test, sizeof(*c), GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, c);
	KUNIT_ASSERT_EQ(test, dpu_color_calib_parse(make_blob(test), BLOB_SIZE, c), 0);
	return c;
}

static void parse_ok(struct kunit *test)
{
	struct dpu_color_calib *c = parsed(test);

	/* same CRC as zlib.crc32(), which the generator uses */
	KUNIT_EXPECT_EQ(test, crc32_le(~0, "123456789", 9) ^ ~0, 0xcbf43926);
	KUNIT_EXPECT_EQ(test, c->gamut[16][0], ramp_gamut(16, 0));
	KUNIT_EXPECT_EQ(test, c->gamut[4912][2], ramp_gamut(4912, 2));
	KUNIT_EXPECT_EQ(test, c->igc[2][256], ramp_igc(2, 256));
	KUNIT_EXPECT_EQ(test, c->gc[1][1023], ramp_gc(1, 1023));
}

static void parse_bad_magic_len_crc(struct kunit *test)
{
	struct dpu_color_calib *c = kunit_kzalloc(test, sizeof(*c), GFP_KERNEL);
	u8 *b = make_blob(test);

	KUNIT_ASSERT_NOT_NULL(test, c);
	KUNIT_EXPECT_EQ(test, dpu_color_calib_parse(b, BLOB_SIZE - 1, c), -EINVAL);
	b[BLOB_GAMUT] ^= 1;
	KUNIT_EXPECT_EQ(test, dpu_color_calib_parse(b, BLOB_SIZE, c), -EBADMSG);
	b[BLOB_GAMUT] ^= 1;
	b[0] = 'X';
	blob_crc(b);
	KUNIT_EXPECT_EQ(test, dpu_color_calib_parse(b, BLOB_SIZE, c), -EINVAL);
}

static void parse_value_too_wide(struct kunit *test)
{
	struct dpu_color_calib *c = kunit_kzalloc(test, sizeof(*c), GFP_KERNEL);
	u8 *b = make_blob(test);

	KUNIT_ASSERT_NOT_NULL(test, c);
	put16(b, BLOB_IGC, 4096);
	blob_crc(b);
	KUNIT_EXPECT_EQ(test, dpu_color_calib_parse(b, BLOB_SIZE, c), -EINVAL);

	b = make_blob(test);
	put16(b, BLOB_GC, 1024);
	blob_crc(b);
	KUNIT_EXPECT_EQ(test, dpu_color_calib_parse(b, BLOB_SIZE, c), -EINVAL);
}

static void igc_stream_layout(struct kunit *test)
{
	struct dpu_color_calib *c = parsed(test);
	u16 *s = kunit_kzalloc(test, DPU_IGC_STREAM_U16 * 2, GFP_KERNEL);
	int i;

	KUNIT_ASSERT_NOT_NULL(test, s);
	memset(s, 0xff, DPU_IGC_STREAM_U16 * 2);
	dpu_color_calib_igc_stream(c, s);
	KUNIT_EXPECT_EQ(test, s[3], ramp_igc(0, 1) << 4);
	KUNIT_EXPECT_EQ(test, s[4], ramp_igc(1, 1) << 4);
	KUNIT_EXPECT_EQ(test, s[5], ramp_igc(2, 1) << 4);
	for (i = 768; i <= 770; i++)
		KUNIT_EXPECT_EQ(test, s[i], 4095 << 4);
	for (i = 771; i <= 773; i++)
		KUNIT_EXPECT_EQ(test, s[i], 0);
}

static void gamut_stream_default(struct kunit *test)
{
	struct dpu_color_calib *c = parsed(test);
	u16 *s = kunit_kzalloc(test, DPU_GAMUT_STREAM_U16 * 2, GFP_KERNEL);
	int i;

	KUNIT_ASSERT_NOT_NULL(test, s);
	memset(s, 0xff, DPU_GAMUT_STREAM_U16 * 2);
	dpu_color_calib_gamut_stream(c, DPU_GAMUT_LAYOUT_DEFAULT, s);
	/* [c0, c2, c1] = [G, R, B] */
	KUNIT_EXPECT_EQ(test, s[48], ramp_gamut(16, 1) << 4);
	KUNIT_EXPECT_EQ(test, s[49], ramp_gamut(16, 0) << 4);
	KUNIT_EXPECT_EQ(test, s[50], ramp_gamut(16, 2) << 4);
	for (i = 14739; i < DPU_GAMUT_STREAM_U16; i++)
		KUNIT_EXPECT_EQ(test, s[i], 0);
}

static void gamut_stream_swap_axes(struct kunit *test)
{
	struct dpu_color_calib *c = parsed(test);
	u16 *s = kunit_kzalloc(test, DPU_GAMUT_STREAM_U16 * 2, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, s);
	dpu_color_calib_gamut_stream(c, DPU_GAMUT_LAYOUT_SWAP_RB_AXES, s);
	KUNIT_EXPECT_EQ(test, s[48], ramp_gamut(289 * 16, 1) << 4);
	KUNIT_EXPECT_EQ(test, s[49], ramp_gamut(289 * 16, 0) << 4);
	KUNIT_EXPECT_EQ(test, s[50], ramp_gamut(289 * 16, 2) << 4);
}

static void gc_words(struct kunit *test)
{
	struct dpu_color_calib *c = parsed(test);
	u32 *w = kunit_kzalloc(test, 3 * 512 * 4, GFP_KERNEL);

	KUNIT_ASSERT_NOT_NULL(test, w);
	dpu_color_calib_gc_words(c, w, w + 512, w + 1024);
	/* c0 = G, c1 = B, c2 = R */
	KUNIT_EXPECT_EQ(test, w[0], ramp_gc(1, 0) | ramp_gc(1, 1) << 16);
	KUNIT_EXPECT_EQ(test, w[512 + 1], ramp_gc(2, 2) | ramp_gc(2, 3) << 16);
	KUNIT_EXPECT_EQ(test, w[1024], ramp_gc(0, 0) | ramp_gc(0, 1) << 16);
}

static struct kunit_case dpu_color_calib_cases[] = {
	KUNIT_CASE(parse_ok),
	KUNIT_CASE(parse_bad_magic_len_crc),
	KUNIT_CASE(parse_value_too_wide),
	KUNIT_CASE(igc_stream_layout),
	KUNIT_CASE(gamut_stream_default),
	KUNIT_CASE(gamut_stream_swap_axes),
	KUNIT_CASE(gc_words),
	{}
};

static struct kunit_suite dpu_color_calib_suite = {
	.name = "dpu_color_calib",
	.test_cases = dpu_color_calib_cases,
};

kunit_test_suite(dpu_color_calib_suite);

MODULE_DESCRIPTION("KUnit tests for the DPU DSPP colour calibration");
MODULE_LICENSE("GPL");
