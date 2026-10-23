// SPDX-License-Identifier: GPL-2.0-only
/*
 * Default DSPP colour calibration: firmware loading and debugfs.
 *
 * The display driver probes before the root filesystem is mounted, so the
 * firmware is requested from a delayed work that retries for a while.
 */

#include <linux/debugfs.h>
#include <linux/firmware.h>
#include <linux/kstrtox.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "dpu_color_calib.h"
#include "dpu_hw_catalog.h"
#include "dpu_kms.h"
#include "dpu_kms_color.h"

#define CALIB_RETRY_DELAY	(2 * HZ)
#define CALIB_MAX_TRIES		90

static struct dpu_kms *color_to_kms(struct dpu_kms_color *c)
{
	return container_of(c, struct dpu_kms, color);
}

/* Rebuilds the streams from the parsed calibration; caller holds the lock */
static void dpu_kms_color_pack(struct dpu_kms_color *c)
{
	dpu_color_calib_igc_stream(c->calib, c->igc_stream);
	dpu_color_calib_gamut_stream(c->calib, c->layout, c->gamut_stream);
	dpu_color_calib_gc_words(c->calib, c->gc.c0, c->gc.c1, c->gc.c2);
	c->gen++;
}

static void dpu_kms_color_load_work(struct work_struct *work)
{
	struct dpu_kms_color *c = container_of(to_delayed_work(work), struct dpu_kms_color, work);
	struct device *dev = color_to_kms(c)->dev->dev;
	const struct firmware *fw;
	int ret;

	ret = request_firmware_direct(&fw, c->fw_name, dev);
	if (ret) {
		if (++c->tries < CALIB_MAX_TRIES) {
			schedule_delayed_work(&c->work, CALIB_RETRY_DELAY);
			return;
		}
		dev_warn(dev, "colour calibration %s not loaded: %d\n", c->fw_name, ret);
		mutex_lock(&c->lock);
		if (c->loaded) {
			c->loaded = false;
			c->gen++;
		}
		mutex_unlock(&c->lock);
		return;
	}

	mutex_lock(&c->lock);
	ret = dpu_color_calib_parse(fw->data, fw->size, c->calib);
	if (ret) {
		c->loaded = false;
		c->gen++;
		dev_warn(dev, "colour calibration %s is invalid: %d\n", c->fw_name, ret);
	} else {
		c->loaded = true;
		c->failed = false;
		dpu_kms_color_pack(c);
		dev_info(dev, "colour calibration %s loaded\n", c->fw_name);
	}
	mutex_unlock(&c->lock);
	release_firmware(fw);
}

void dpu_kms_color_init(struct dpu_kms *kms)
{
	struct dpu_kms_color *c = &kms->color;

	mutex_init(&c->lock);
	INIT_DELAYED_WORK(&c->work, dpu_kms_color_load_work);
	c->layout = DPU_GAMUT_LAYOUT_DEFAULT;

	if (!kms->lutdma ||
	    of_property_read_string(kms->pdev->dev.of_node, "qcom,dspp-calibration", &c->fw_name)) {
		c->fw_name = NULL;
		return;
	}

	c->calib = kvzalloc(sizeof(*c->calib), GFP_KERNEL);
	c->igc_stream = kvcalloc(DPU_IGC_STREAM_U16, sizeof(u16), GFP_KERNEL);
	c->gamut_stream = kvcalloc(DPU_GAMUT_STREAM_U16, sizeof(u16), GFP_KERNEL);
	if (!c->calib || !c->igc_stream || !c->gamut_stream) {
		dpu_kms_color_fini(kms);
		return;
	}

	schedule_delayed_work(&c->work, 0);
}

void dpu_kms_color_fini(struct dpu_kms *kms)
{
	struct dpu_kms_color *c = &kms->color;

	if (!c->fw_name)
		return;
	cancel_delayed_work_sync(&c->work);
	kvfree(c->calib);
	kvfree(c->igc_stream);
	kvfree(c->gamut_stream);
	c->calib = NULL;
	c->igc_stream = NULL;
	c->gamut_stream = NULL;
	c->loaded = false;
	c->fw_name = NULL;
}

static int dpu_kms_color_show(struct seq_file *s, void *unused)
{
	struct dpu_kms *kms = s->private;
	struct dpu_kms_color *c = &kms->color;
	const struct dpu_dspp_cfg *dspp = &kms->catalog->dspp[0];
	void __iomem *base = kms->mmio + dspp->base;
	u32 igc = 0, gamut = 0, gc = 0, pcc = 0;

	if (kms->catalog->dspp_count && pm_runtime_get_if_active(&kms->pdev->dev) > 0) {
		if (dspp->sblk->igc.base)
			igc = readl(base + dspp->sblk->igc.base + 0x4);
		if (dspp->sblk->gamut.base)
			gamut = readl(base + dspp->sblk->gamut.base);
		gc = readl(base + dspp->sblk->gc.base);
		pcc = readl(base + dspp->sblk->pcc.base);
		pm_runtime_put(&kms->pdev->dev);
	}

	mutex_lock(&c->lock);
	seq_printf(s, "mode=%d fw=%s loaded=%d failed=%d gen=%u layout=%u last_rc=%d lutdma_err=0x%x\n",
		   READ_ONCE(dpu_default_ctm), c->fw_name ?: "-", c->loaded, c->failed, c->gen,
		   c->layout, c->last_rc,
		   kms->lutdma ? dpu_hw_lutdma_error_status(kms->lutdma) : 0);
	mutex_unlock(&c->lock);
	seq_printf(s, "igc_opmode=0x%x gamut_opmode=0x%x gc_en=0x%x pcc_en=0x%x\n",
		   igc, gamut, gc, pcc);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dpu_kms_color);

static ssize_t dpu_kms_color_reload_write(struct file *file, const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	struct dpu_kms_color *c = &((struct dpu_kms *)file->private_data)->color;

	if (!c->fw_name)
		return -ENODEV;
	mutex_lock(&c->lock);
	c->failed = false;
	c->tries = CALIB_MAX_TRIES - 1;	/* one try, no retries */
	mutex_unlock(&c->lock);
	mod_delayed_work(system_wq, &c->work, 0);
	return count;
}

static const struct file_operations dpu_kms_color_reload_fops = {
	.open = simple_open,
	.write = dpu_kms_color_reload_write,
};

static int dpu_kms_color_layout_get(void *data, u64 *val)
{
	*val = ((struct dpu_kms *)data)->color.layout;
	return 0;
}

static int dpu_kms_color_layout_set(void *data, u64 val)
{
	struct dpu_kms_color *c = &((struct dpu_kms *)data)->color;

	if (val > U32_MAX)
		return -EINVAL;
	mutex_lock(&c->lock);
	c->layout = val;
	if (c->loaded)
		dpu_kms_color_pack(c);
	mutex_unlock(&c->lock);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(dpu_kms_color_layout_fops, dpu_kms_color_layout_get,
			 dpu_kms_color_layout_set, "%llu\n");

void dpu_kms_color_debugfs_init(struct dpu_kms *kms, struct dentry *parent)
{
	debugfs_create_file("dspp_color", 0400, parent, kms, &dpu_kms_color_fops);
	debugfs_create_file("dspp_color_reload", 0200, parent, kms, &dpu_kms_color_reload_fops);
	debugfs_create_file("dspp_gamut_layout", 0600, parent, kms, &dpu_kms_color_layout_fops);
}
