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

/* An idle touch report: everything 0xff except the register 0x01 byte */
static void blank_report(u8 *buf)
{
	memset(buf, 0xff, FT3683G_TOUCH_LEN);
	buf[0] = 0x00;
}

static void set_record(u8 *buf, int off, const u8 *rec, int len)
{
	memcpy(buf + off, rec, len);
}

static void expect_contact(struct kunit *test, const struct ft3683g_contact *c,
			   u8 id, bool down, u16 x, u16 y, u8 major)
{
	KUNIT_EXPECT_EQ(test, c->id, id);
	KUNIT_EXPECT_EQ(test, c->down, down);
	if (down) {
		KUNIT_EXPECT_EQ(test, c->x, x);
		KUNIT_EXPECT_EQ(test, c->y, y);
		KUNIT_EXPECT_EQ(test, c->major, major);
	}
}

static const u8 t0_down_612_1360[] = { 0x02, 0x64, 0x05, 0x50, 0x00, 0x0a };
static const u8 t2_down_612_1360[] = { 0x02, 0x64, 0x05, 0x50, 0x00, 0x0c, 0x0c, 0x00 };

static void parse_t0_one_down(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n = 99;

	blank_report(buf);
	buf[1] = 0x01;
	set_record(buf, 2, t0_down_612_1360, 6);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 1);
	expect_contact(test, &c[0], 0, true, 612, 1360, 10);
}

static void parse_t0_area_zero(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x01;
	set_record(buf, 2, t0_down_612_1360, 6);
	buf[7] = 0x00;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 1);
	KUNIT_EXPECT_EQ(test, c[0].major, 9);
}

static void parse_t0_two_one_up(struct kunit *test)
{
	static const u8 contact0[] = { 0x82, 0x64, 0x05, 0x50, 0x00, 0x0a };
	static const u8 up1[] = { 0x40, 0x10, 0x10, 0x20, 0x00, 0x05 };
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x02;
	set_record(buf, 2, contact0, 6);
	set_record(buf, 8, up1, 6);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 2);
	expect_contact(test, &c[0], 0, true, 612, 1360, 10);
	expect_contact(test, &c[1], 1, false, 0, 0, 0);
}

static void parse_t0_flag3_released(struct kunit *test)
{
	static const u8 rec[] = { 0xc2, 0x64, 0x05, 0x50, 0x00, 0x0a };
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x01;
	set_record(buf, 2, rec, 6);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 1);
	expect_contact(test, &c[0], 0, false, 0, 0, 0);
}

static void parse_t0_too_many(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x0b;
	set_record(buf, 2, t0_down_612_1360, 6);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), -EINVAL);
}

static void parse_t0_down_but_zero_count(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x00;
	set_record(buf, 2, t0_down_612_1360, 6);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), -EINVAL);
}

static void parse_t0_no_records(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x01;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), -EINVAL);
}

static void parse_t2_one_down(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x21;
	buf[2] = 0x00;
	buf[3] = 0x00;
	set_record(buf, 4, t2_down_612_1360, 8);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 1);
	expect_contact(test, &c[0], 0, true, 612, 1360, 12);
}

static void parse_t2_subpixel_truncates(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0x21;
	buf[2] = 0x00;
	buf[3] = 0x00;
	set_record(buf, 4, t2_down_612_1360, 8);
	buf[8] = 0x8f;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_TOUCH);
	KUNIT_ASSERT_EQ(test, n, 1);
	expect_contact(test, &c[0], 0, true, 612, 1360, 12);
}

static void parse_t2_bad_count(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[2] = 0x00;
	buf[3] = 0x00;
	set_record(buf, 4, t2_down_612_1360, 8);
	buf[1] = 0x20;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), -EINVAL);
	buf[1] = 0x2b;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), -EINVAL);
}

static void parse_fw_init(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n = 99;

	blank_report(buf);
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_FW_INIT);
	KUNIT_EXPECT_EQ(test, n, 0);
}

static void parse_pen_ignored(struct kunit *test)
{
	struct ft3683g_contact c[FT3683G_MAX_CONTACTS];
	u8 buf[FT3683G_TOUCH_LEN];
	unsigned int n;

	blank_report(buf);
	buf[1] = 0xb1;
	buf[2] = 0x00;
	KUNIT_EXPECT_EQ(test, ft3683g_parse_touch(buf, c, &n), FT3683G_PACKET_IGNORED);
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
	KUNIT_CASE(parse_t0_one_down),
	KUNIT_CASE(parse_t0_area_zero),
	KUNIT_CASE(parse_t0_two_one_up),
	KUNIT_CASE(parse_t0_flag3_released),
	KUNIT_CASE(parse_t0_too_many),
	KUNIT_CASE(parse_t0_down_but_zero_count),
	KUNIT_CASE(parse_t0_no_records),
	KUNIT_CASE(parse_t2_one_down),
	KUNIT_CASE(parse_t2_subpixel_truncates),
	KUNIT_CASE(parse_t2_bad_count),
	KUNIT_CASE(parse_fw_init),
	KUNIT_CASE(parse_pen_ignored),
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
