/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FocalTech FT3683G SPI touchscreen: protocol helpers
 */

#ifndef _FOCALTECH_FT3683G_H
#define _FOCALTECH_FT3683G_H

#include <linux/types.h>

#define FT3683G_REG_TOUCH	0x01
#define FT3683G_REG_CHIP_ID	0xa3	/* 0x56 */
#define FT3683G_REG_CHIP_ID2	0x9f	/* 0x72 */
#define FT3683G_REG_FW_VER	0xa6
#define FT3683G_REG_VENDOR	0xa8	/* 0xda = BOE, 0x44 = Visionox */

#define FT3683G_MAX_CONTACTS	10
#define FT3683G_TOUCH_LEN	84

/* A read frame is a 4 byte header, 3 dummy bytes, the data and a CRC16 */
#define FT3683G_READ_DATA_OFF	7
#define FT3683G_READ_OVERHEAD	9

struct ft3683g_contact {
	u8 id;
	bool down;
	u16 x;
	u16 y;
	u8 major;
};

enum ft3683g_packet {
	FT3683G_PACKET_TOUCH,		/* contacts were filled in */
	FT3683G_PACKET_FW_INIT,		/* firmware (re)started: release all */
	FT3683G_PACKET_IGNORED,		/* pen, debug or unknown event */
};

size_t ft3683g_build_read(u8 *tx, u8 reg, u16 len);
int ft3683g_check_read(const u8 *rx, u16 len);
int ft3683g_parse_touch(const u8 *buf, struct ft3683g_contact *c,
			unsigned int *n);

#endif
