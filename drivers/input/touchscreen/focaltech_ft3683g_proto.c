// SPDX-License-Identifier: GPL-2.0
/*
 * FocalTech FT3683G SPI touchscreen: protocol helpers
 *
 * The SPI framing follows FocalTech's "SPI v2" protocol. A read frame is
 *
 *   reg, 0xa0 (read | CRC enable), len[15:8], len[7:0], 3 dummy bytes,
 *   len data bytes, CRC16 (low byte first)
 *
 * clocked out as one full-duplex transfer. The status byte returned while
 * len[7:0] is sent must have bits 7 and 5 clear. The CRC is CRC-16/MCRF4XX
 * (reflected 0x8408, init 0xffff) over the data bytes only.
 */

#include <linux/crc-ccitt.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/string.h>

#include "focaltech_ft3683g.h"

#define FT3683G_CTRL_READ_CRC	0xa0
#define FT3683G_STATUS_ERR_MASK	0xa0

size_t ft3683g_build_read(u8 *tx, u8 reg, u16 len)
{
	size_t size = len + FT3683G_READ_OVERHEAD;

	memset(tx, 0, size);
	tx[0] = reg;
	tx[1] = FT3683G_CTRL_READ_CRC;
	tx[2] = len >> 8;
	tx[3] = len & 0xff;

	return size;
}
EXPORT_SYMBOL_NS_GPL(ft3683g_build_read, "FT3683G");

int ft3683g_check_read(const u8 *rx, u16 len)
{
	const u8 *data = rx + FT3683G_READ_DATA_OFF;
	u16 crc = data[len] | data[len + 1] << 8;

	if (rx[3] & FT3683G_STATUS_ERR_MASK)
		return -EIO;

	if (crc_ccitt(0xffff, data, len) != crc)
		return -EBADMSG;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ft3683g_check_read, "FT3683G");

/*
 * Touch report (register 0x01, 84 bytes):
 *   buf[0]   unused
 *   buf[1]   event type [7:4], point count [3:0]
 *
 * Type 0: 6 byte records from buf[2]:
 *   flag[7:6] x[11:8], x[7:0], id[7:4] y[11:8], y[7:0], weight, area
 * Type 2: 8 byte records from buf[4], coordinates in 1/16 pixel:
 *   flag[7:6] x[15:12], x[11:4], id[7:4] y[15:12], y[11:4],
 *   x[3:0] y[3:0], major, minor, unused
 *
 * Flag 0 is down, 1 is up, 2 is contact (move); 3 is treated as up.
 */
#define FT3683G_TYPE_DEFAULT	0
#define FT3683G_TYPE_V2		2
#define FT3683G_REC_LEN		6
#define FT3683G_REC_V2_LEN	8
#define FT3683G_DEFAULT_AREA	9

static bool ft3683g_flag_down(u8 b)
{
	u8 flag = b >> 6;

	return flag == 0 || flag == 2;
}

static u8 ft3683g_area(u8 area)
{
	return area ?: FT3683G_DEFAULT_AREA;
}

static int ft3683g_parse_default(const u8 *buf, struct ft3683g_contact *c,
				 unsigned int *n)
{
	unsigned int count = buf[1] & 0x0f;
	bool any_down = false;
	unsigned int i;

	if (count > FT3683G_MAX_CONTACTS)
		return -EINVAL;

	for (i = 0; i < FT3683G_MAX_CONTACTS; i++) {
		const u8 *r = buf + 2 + i * FT3683G_REC_LEN;
		u8 id = r[2] >> 4;

		if (id >= FT3683G_MAX_CONTACTS)
			break;

		c[i].id = id;
		c[i].down = ft3683g_flag_down(r[0]);
		c[i].x = (r[0] & 0x0f) << 8 | r[1];
		c[i].y = (r[2] & 0x0f) << 8 | r[3];
		c[i].major = ft3683g_area(r[5]);
		any_down |= c[i].down;
	}

	if (!i || (any_down && !count))
		return -EINVAL;

	*n = i;
	return FT3683G_PACKET_TOUCH;
}

static int ft3683g_parse_v2(const u8 *buf, struct ft3683g_contact *c,
			    unsigned int *n)
{
	unsigned int count = buf[1] & 0x0f;
	unsigned int i;

	if (!count || count > FT3683G_MAX_CONTACTS)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		const u8 *r = buf + 4 + i * FT3683G_REC_V2_LEN;

		c[i].id = r[2] >> 4;
		if (c[i].id >= FT3683G_MAX_CONTACTS)
			return -EINVAL;

		c[i].down = ft3683g_flag_down(r[0]);
		c[i].x = ((r[0] & 0x0f) << 12 | r[1] << 4 | r[4] >> 4) / 16;
		c[i].y = ((r[2] & 0x0f) << 12 | r[3] << 4 | (r[4] & 0x0f)) / 16;
		c[i].major = ft3683g_area(r[5]);
	}

	*n = count;
	return FT3683G_PACKET_TOUCH;
}

int ft3683g_parse_touch(const u8 *buf, struct ft3683g_contact *c,
			unsigned int *n)
{
	*n = 0;

	if (buf[1] == 0xff && buf[2] == 0xff && buf[3] == 0xff && buf[4] == 0xff)
		return FT3683G_PACKET_FW_INIT;

	switch (buf[1] >> 4) {
	case FT3683G_TYPE_DEFAULT:
		return ft3683g_parse_default(buf, c, n);
	case FT3683G_TYPE_V2:
		return ft3683g_parse_v2(buf, c, n);
	default:
		return FT3683G_PACKET_IGNORED;
	}
}
EXPORT_SYMBOL_NS_GPL(ft3683g_parse_touch, "FT3683G");

MODULE_DESCRIPTION("FocalTech FT3683G protocol helpers");
MODULE_LICENSE("GPL");
