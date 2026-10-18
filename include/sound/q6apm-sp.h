/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Speaker protection state reported by the Qualcomm AudioReach APM.
 *
 * ACTIVE only while a started playback graph with the speaker protection
 * module and a started feedback graph with its V/I module are both running,
 * with their control link accepted by the DSP. Anything else is INACTIVE.
 */
#ifndef __SOUND_Q6APM_SP_H__
#define __SOUND_Q6APM_SP_H__

struct notifier_block;

#define Q6APM_SP_INACTIVE	0
#define Q6APM_SP_ACTIVE		1

/* The callback is called once with the current state on registration */
int q6apm_sp_notifier_register(struct notifier_block *nb);
int q6apm_sp_notifier_unregister(struct notifier_block *nb);

#endif
