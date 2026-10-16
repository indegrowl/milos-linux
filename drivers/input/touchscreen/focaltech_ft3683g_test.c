// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the FocalTech FT3683G protocol helpers
 */

#include <kunit/test.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "focaltech_ft3683g.h"

/* rx frame for a read with the given data and CRC (low byte first) */
static void make_rx(u8 *rx, const u8 *data, u16 len, u8 crc_lo, u8 crc_hi)
{
	memset(rx, 0, FT3683G_READ_OVERHEAD + len);
	memcpy(rx + FT3683G_READ_DATA_OFF, data, len);
	rx[FT3683G_READ_DATA_OFF + len] = crc_lo;
	rx[FT3683G_READ_DATA_OFF + len + 1] = crc_hi;
}

static void build_read_chip_id(struct kunit *test)
{
	static const u8 want[] = { 0xa3, 0xa0, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
	u8 tx[16];

	memset(tx, 0x5a, sizeof(tx));
	KUNIT_EXPECT_EQ(test, ft3683g_build_read(tx, 0xa3, 1), 10);
	KUNIT_EXPECT_MEMEQ(test, tx, want, sizeof(want));
}

static void build_read_touch(struct kunit *test)
{
	u8 tx[FT3683G_TOUCH_LEN + FT3683G_READ_OVERHEAD];
	int i;

	memset(tx, 0x5a, sizeof(tx));
	KUNIT_EXPECT_EQ(test, ft3683g_build_read(tx, 0x01, 84), 93);
	KUNIT_EXPECT_EQ(test, tx[0], 0x01);
	KUNIT_EXPECT_EQ(test, tx[1], 0xa0);
	KUNIT_EXPECT_EQ(test, tx[2], 0x00);
	KUNIT_EXPECT_EQ(test, tx[3], 0x54);
	for (i = 4; i < 93; i++)
		KUNIT_EXPECT_EQ(test, tx[i], 0);
}

static void check_read_ok_56(struct kunit *test)
{
	u8 d = 0x56, rx[10];

	make_rx(rx, &d, 1, 0x34, 0x38);
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), 0);
}

static void check_read_ok_72(struct kunit *test)
{
	u8 d = 0x72, rx[10];

	make_rx(rx, &d, 1, 0x12, 0x5f);
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), 0);
}

static void check_read_ok_123456789(struct kunit *test)
{
	u8 rx[18];

	make_rx(rx, (const u8 *)"123456789", 9, 0x91, 0x6f);
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 9), 0);
}

static void check_read_status_busy(struct kunit *test)
{
	u8 d = 0x56, rx[10];

	make_rx(rx, &d, 1, 0x34, 0x38);
	rx[3] = 0x80;
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), -EIO);
	rx[3] = 0x20;
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), -EIO);
}

static void check_read_status_other_bits(struct kunit *test)
{
	u8 d = 0x56, rx[10];

	make_rx(rx, &d, 1, 0x34, 0x38);
	rx[3] = 0x40;
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), 0);
}

static void check_read_bus_high(struct kunit *test)
{
	u8 rx[10];

	memset(rx, 0xff, sizeof(rx));
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), -EIO);
}

static void check_read_bus_low(struct kunit *test)
{
	u8 rx[10] = { };

	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), -EBADMSG);
}

static void check_read_bad_crc(struct kunit *test)
{
	u8 d = 0x56, rx[10];

	make_rx(rx, &d, 1, 0x34, 0x38);
	rx[8] = 0x35;
	KUNIT_EXPECT_EQ(test, ft3683g_check_read(rx, 1), -EBADMSG);
}

static struct kunit_case ft3683g_test_cases[] = {
	KUNIT_CASE(build_read_chip_id),
	KUNIT_CASE(build_read_touch),
	KUNIT_CASE(check_read_ok_56),
	KUNIT_CASE(check_read_ok_72),
	KUNIT_CASE(check_read_ok_123456789),
	KUNIT_CASE(check_read_status_busy),
	KUNIT_CASE(check_read_status_other_bits),
	KUNIT_CASE(check_read_bus_high),
	KUNIT_CASE(check_read_bus_low),
	KUNIT_CASE(check_read_bad_crc),
	{ }
};

static struct kunit_suite ft3683g_test_suite = {
	.name = "ft3683g",
	.test_cases = ft3683g_test_cases,
};
kunit_test_suite(ft3683g_test_suite);

MODULE_IMPORT_NS("FT3683G");
MODULE_DESCRIPTION("KUnit tests for the FocalTech FT3683G protocol helpers");
MODULE_LICENSE("GPL");
