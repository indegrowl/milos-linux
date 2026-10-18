// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the AW88271 volume cap and protection interlock helpers.
 */

#include <kunit/test.h>
#include <linux/minmax.h>

#include "aw88271-cap.h"

static void aw88271_att_min_unprotected(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_att_min(AW88271_ROLE_SPEAKER, true, false),
			AW88271_ATT_MIN_1A);
	KUNIT_EXPECT_EQ(test, aw88271_att_min(AW88271_ROLE_SPEAKER, false, true),
			AW88271_ATT_MIN_1A);
	KUNIT_EXPECT_EQ(test, aw88271_att_min(AW88271_ROLE_EARPIECE, false, false),
			AW88271_ATT_MIN_1A);
}

static void aw88271_att_min_protected(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_att_min(AW88271_ROLE_SPEAKER, true, true),
			max(AW88271_ATT_MIN_PROT_SPK, AW88271_ATT_STOCK));
}

static void aw88271_att_min_earpiece_not_louder(struct kunit *test)
{
	KUNIT_EXPECT_GE(test, aw88271_att_min(AW88271_ROLE_EARPIECE, true, true),
			aw88271_att_min(AW88271_ROLE_SPEAKER, true, true));
}

static void aw88271_att_min_never_below_stock(struct kunit *test)
{
	int role, vi, sp;

	for (role = AW88271_ROLE_EARPIECE; role <= AW88271_ROLE_SPEAKER; role++)
		for (vi = 0; vi <= 1; vi++)
			for (sp = 0; sp <= 1; sp++)
				KUNIT_EXPECT_GE(test, aw88271_att_min(role, vi, sp),
						AW88271_ATT_STOCK);
}

static void aw88271_vol_zero_is_mute(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_vol_to_att(0, 700), AW88271_VOL_MUTE);
}

static void aw88271_vol_max_is_cap(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_vol_to_att(AW88271_VOL_CTL_MAX, 700), 700);
}

static void aw88271_vol_overreach_clamped(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_vol_to_att(5000, 700), 700);
}

static void aw88271_acf_filter_volume(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_acf_filter(0x05, 0x8017, 700), 0x8000 | 700);
}

static void aw88271_acf_filter_hmute(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, aw88271_acf_filter(0x04, 0x3A40, 700) & BIT(8));
}

static void aw88271_acf_filter_passthrough(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, aw88271_acf_filter(0x06, 0x34E8, 700), 0x34E8);
}

static struct kunit_case aw88271_cap_cases[] = {
	KUNIT_CASE(aw88271_att_min_unprotected),
	KUNIT_CASE(aw88271_att_min_protected),
	KUNIT_CASE(aw88271_att_min_earpiece_not_louder),
	KUNIT_CASE(aw88271_att_min_never_below_stock),
	KUNIT_CASE(aw88271_vol_zero_is_mute),
	KUNIT_CASE(aw88271_vol_max_is_cap),
	KUNIT_CASE(aw88271_vol_overreach_clamped),
	KUNIT_CASE(aw88271_acf_filter_volume),
	KUNIT_CASE(aw88271_acf_filter_hmute),
	KUNIT_CASE(aw88271_acf_filter_passthrough),
	{}
};

static struct kunit_suite aw88271_cap_suite = {
	.name = "aw88271-cap",
	.test_cases = aw88271_cap_cases,
};
kunit_test_suite(aw88271_cap_suite);

MODULE_DESCRIPTION("KUnit tests for the AW88271 volume cap");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
