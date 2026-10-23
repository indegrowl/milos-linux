/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Default DSPP colour calibration: loaded from firmware named by the DT
 * property "qcom,dspp-calibration" and applied by the CRTC when userspace
 * sets no CTM or gamma LUT (msm.dpu_default_ctm=3).
 */

#ifndef _DPU_KMS_COLOR_H
#define _DPU_KMS_COLOR_H

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "dpu_hw_dspp.h"

struct dentry;
struct dpu_kms;

struct dpu_kms_color {
	struct mutex lock;		/* protects everything below */
	const char *fw_name;		/* NULL: no calibration for this device */
	struct delayed_work work;
	int tries;
	bool loaded;
	bool failed;			/* a LUTDMA submit failed; cleared by reload */
	u32 gen;			/* bumped whenever the streams change */
	u32 layout;			/* gamut stream layout, DPU_GAMUT_LAYOUT_* */
	int last_rc;			/* last LUTDMA submit */
	struct dpu_color_calib *calib;
	u16 *igc_stream;
	u16 *gamut_stream;
	struct dpu_hw_gc_lut gc;
};

void dpu_kms_color_init(struct dpu_kms *kms);
void dpu_kms_color_fini(struct dpu_kms *kms);
void dpu_kms_color_debugfs_init(struct dpu_kms *kms, struct dentry *parent);

#endif /* _DPU_KMS_COLOR_H */
