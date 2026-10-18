/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * awinic AW88271 smart amplifier
 *
 * Register meanings come from Nothing's GPL aw882xx vendor driver (chip ID
 * 0x2329 uses its common register map); there is no public datasheet.
 */

#ifndef __AW88271_H__
#define __AW88271_H__

#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>

#include "aw88271-cap.h"

#define AW88271_CHIP_ID			0x2329
#define AW88271_REG_MAX			0x7f

#define AW88271_REG_ID			0x00	/* write: soft reset */
#define AW88271_SOFT_RESET		0x55aa
#define AW88271_REG_SYSST		0x01
#define AW88271_REG_SYSINT		0x02
#define AW88271_REG_SYSINTM		0x03
/* AW88271_REG_SYSCTRL (0x04) and AW88271_REG_SYSCTRL2 (0x05) in aw88271-cap.h */

/* SYSST */
#define AW88271_PLLS			BIT(0)
#define AW88271_CLKS			BIT(4)
#define AW88271_BSTS			BIT(9)
#define AW88271_IIS_CHECK		(AW88271_PLLS | AW88271_CLKS)
#define AW88271_SYSST_CHECK		(AW88271_PLLS | AW88271_CLKS | AW88271_BSTS)
#define AW88271_SYSST_RETRIES		10

/* SYSCTRL */
#define AW88271_PWDN			BIT(0)
#define AW88271_AMPPD			BIT(1)
#define AW88271_I2C_WEN_MASK		GENMASK(11, 10)
#define AW88271_I2C_WEN_ENABLE		(2 << 10)
#define AW88271_I2STXEN			BIT(13)
#define AW88271_ULS_HMUTE		BIT(15)

#define AW88271_SYSINTM_ALL		0xffff

#define AW88271_START_RETRIES		3
#define AW88271_START_DELAY_MS		10

struct aw_device;
struct aw_container;
struct dentry;

struct aw88271 {
	struct aw_device *aw_pa;
	struct regmap *regmap;
	struct aw_container *aw_cfg;
	struct gpio_desc *reset_gpio;
	struct mutex lock;		/* protects everything below */
	struct delayed_work start_work;
	struct dentry *debugfs;
	struct notifier_block sp_nb;	/* platform speaker protection state */
	enum aw88271_role role;
	unsigned int ctl_vol;		/* 0 (mute) .. AW88271_VOL_CTL_MAX (cap) */
	u16 txen_cfg;			/* I2STXEN as set by the ACF profile */
	bool fw_ok;
	bool powered;
	bool vi_running;
	bool sp_active;
};

int aw88271_set_sp_active(struct aw88271 *aw, bool active);

#endif
