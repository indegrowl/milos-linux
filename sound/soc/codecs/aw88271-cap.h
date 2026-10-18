/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AW88271 volume cap and speaker-protection interlock.
 *
 * Volume is the SYSCTRL2 (0x05) bits 9:0 attenuation, about 0.094 dB per
 * step; 1023 is mute. The cap is the lowest attenuation the driver allows.
 * Until ADSP speaker protection is proven running (V/I capture running and
 * the protection graph reported active), only the unprotected cap applies.
 */

#ifndef __AW88271_CAP_H__
#define __AW88271_CAP_H__

#include <linux/types.h>

enum aw88271_role {
	AW88271_ROLE_EARPIECE = 0,	/* default: the more fragile transducer */
	AW88271_ROLE_SPEAKER = 1,
};

#define AW88271_VOL_MUTE		1023
#define AW88271_VOL_CTL_MAX		1023
/* Stock Nothing OS Music profile volume: never play louder than this */
#define AW88271_ATT_STOCK		23

/* Unprotected cap: barely audible */
#define AW88271_ATT_MIN_1A		380
/* Caps while protection runs; raised only after it is proven working */
#define AW88271_ATT_MIN_PROT_SPK	AW88271_ATT_MIN_1A
#define AW88271_ATT_MIN_PROT_EAR	AW88271_ATT_MIN_1A

#define AW88271_REG_SYSCTRL		0x04
#define AW88271_REG_SYSCTRL2		0x05
#define AW88271_HMUTE			BIT(8)
#define AW88271_VOL_MASK		GENMASK(9, 0)

unsigned int aw88271_att_min(enum aw88271_role role, bool vi_running, bool sp_active);
unsigned int aw88271_vol_to_att(unsigned int ctl, unsigned int att_min);
u16 aw88271_acf_filter(u8 reg, u16 val, unsigned int att);

#endif
