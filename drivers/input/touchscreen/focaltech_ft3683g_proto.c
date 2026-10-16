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

MODULE_DESCRIPTION("FocalTech FT3683G protocol helpers");
MODULE_LICENSE("GPL");
