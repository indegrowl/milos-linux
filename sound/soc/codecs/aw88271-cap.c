// SPDX-License-Identifier: GPL-2.0-only
/*
 * AW88271 volume cap and speaker-protection interlock helpers.
 */

#include <kunit/visibility.h>
#include <linux/bits.h>
#include <linux/minmax.h>
#include <linux/module.h>

#include "aw88271-cap.h"

unsigned int aw88271_att_min(enum aw88271_role role, bool vi_running, bool sp_active)
{
	unsigned int att;

	if (!vi_running || !sp_active)
		att = AW88271_ATT_MIN_1A;
	else if (role == AW88271_ROLE_SPEAKER)
		att = AW88271_ATT_MIN_PROT_SPK;
	else
		att = max(AW88271_ATT_MIN_PROT_EAR, AW88271_ATT_MIN_PROT_SPK);

	return max(att, AW88271_ATT_STOCK);
}
EXPORT_SYMBOL_IF_KUNIT(aw88271_att_min);

unsigned int aw88271_vol_to_att(unsigned int ctl, unsigned int att_min)
{
	ctl = min(ctl, AW88271_VOL_CTL_MAX);

	return AW88271_VOL_MUTE - ctl * (AW88271_VOL_MUTE - att_min) / AW88271_VOL_CTL_MAX;
}
EXPORT_SYMBOL_IF_KUNIT(aw88271_vol_to_att);

u16 aw88271_acf_filter(u8 reg, u16 val, unsigned int att)
{
	switch (reg) {
	case AW88271_REG_SYSCTRL2:
		return (val & ~AW88271_VOL_MASK) | (att & AW88271_VOL_MASK);
	case AW88271_REG_SYSCTRL:
		return val | AW88271_HMUTE;
	default:
		return val;
	}
}
EXPORT_SYMBOL_IF_KUNIT(aw88271_acf_filter);
