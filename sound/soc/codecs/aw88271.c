// SPDX-License-Identifier: GPL-2.0-only
/*
 * awinic AW88271 smart amplifier driver
 *
 * The AW88271 has no on-chip DSP. Its tuning comes from the vendor ACF file
 * (register writes per profile); speaker protection, when present, runs on
 * the host DSP and is fed by the amp's V/I sense output. This driver never
 * talks to that DSP: it only caps its own volume until the platform reports
 * that protection is running and the V/I capture is active.
 *
 * Power and register sequencing follow Nothing's GPL aw882xx driver for chip
 * ID 0x2329.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/q6apm-sp.h>
#include <sound/soc.h>

#include "aw88395/aw88395_device.h"
#include "aw88271.h"

static const struct regmap_config aw88271_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = AW88271_REG_MAX,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static unsigned int aw88271_cur_att(struct aw88271 *aw)
{
	unsigned int att_min = aw88271_att_min(aw->role, aw->vi_running, aw->sp_active);

	return aw88271_vol_to_att(aw->ctl_vol, att_min);
}

/*
 * Must hold aw->lock. Writes the capped volume, whatever the power state. If
 * that fails the amp may still be at a higher level than now allowed, so
 * hard-mute it instead.
 */
static int aw88271_apply_volume(struct aw88271 *aw)
{
	int ret;

	ret = regmap_update_bits(aw->regmap, AW88271_REG_SYSCTRL2, AW88271_VOL_MASK,
				 aw88271_cur_att(aw));
	if (ret) {
		dev_err_ratelimited(aw->aw_pa->dev, "volume write failed (%d), muting\n", ret);
		regmap_update_bits(aw->regmap, AW88271_REG_SYSCTRL, AW88271_HMUTE, AW88271_HMUTE);
	}

	return ret;
}

static int aw88271_sysctrl_bits(struct aw88271 *aw, unsigned int mask, bool set)
{
	return regmap_update_bits(aw->regmap, AW88271_REG_SYSCTRL, mask, set ? mask : 0);
}

static int aw88271_wait_sysst(struct aw88271 *aw, unsigned int check)
{
	unsigned int val;
	int i, ret;

	for (i = 0; i < AW88271_SYSST_RETRIES; i++) {
		ret = regmap_read(aw->regmap, AW88271_REG_SYSST, &val);
		if (ret)
			return ret;
		if ((val & check) == check)
			return 0;
		usleep_range(2000, 2010);
	}
	dev_dbg(aw->aw_pa->dev, "sysst 0x%04x, want 0x%04x\n", val, check);

	return -ETIMEDOUT;
}

static int aw88271_find_profile(struct aw88271 *aw, const char *name)
{
	struct aw_prof_info *prof_info = &aw->aw_pa->prof_info;
	int i;

	for (i = 0; i < prof_info->count; i++) {
		const char *p = prof_info->prof_name_list[prof_info->prof_desc[i].id];

		if (!strcmp(p, name))
			return i;
	}

	return -ENOENT;
}

/*
 * Write one ACF profile. The amp stays powered down, hard-muted and with its
 * V/I output off; the volume field is always the current capped attenuation.
 */
static int aw88271_write_profile(struct aw88271 *aw)
{
	struct aw_device *aw_dev = aw->aw_pa;
	struct aw_sec_data_desc *reg_sec;
	unsigned int att = aw88271_cur_att(aw);
	u16 *data;
	int i, n, ret;

	reg_sec = &aw_dev->prof_info.prof_desc[aw_dev->prof_index].sec_desc[AW88395_DATA_TYPE_REG];
	if (!reg_sec->data || !reg_sec->len || (reg_sec->len % 4))
		return -EINVAL;

	data = (u16 *)reg_sec->data;
	n = reg_sec->len / 2;
	for (i = 0; i < n; i += 2) {
		u8 reg = data[i];
		u16 val = aw88271_acf_filter(reg, data[i + 1], att);

		if (data[i] > AW88271_REG_MAX)
			continue;

		switch (reg) {
		case AW88271_REG_SYSINTM:
			val = AW88271_SYSINTM_ALL;
			break;
		case AW88271_REG_SYSCTRL:
			aw->txen_cfg = val & AW88271_I2STXEN;
			val &= ~(AW88271_I2STXEN | AW88271_I2C_WEN_MASK);
			val |= AW88271_PWDN | AW88271_AMPPD | AW88271_HMUTE |
			       AW88271_ULS_HMUTE | AW88271_I2C_WEN_ENABLE;
			break;
		default:
			break;
		}

		ret = regmap_write(aw->regmap, reg, val);
		if (ret)
			return ret;
	}

	return aw88271_apply_volume(aw);
}

/* Must hold aw->lock */
static void aw88271_power_off(struct aw88271 *aw)
{
	aw88271_sysctrl_bits(aw, AW88271_HMUTE | AW88271_ULS_HMUTE, true);
	aw88271_sysctrl_bits(aw, AW88271_I2STXEN, false);
	usleep_range(1000, 1100);
	aw88271_sysctrl_bits(aw, AW88271_AMPPD | AW88271_PWDN, true);
	aw->powered = false;
}

/* Must hold aw->lock */
static int aw88271_power_on(struct aw88271 *aw)
{
	unsigned int val;
	int ret;

	ret = regmap_write(aw->regmap, AW88271_REG_ID, AW88271_SOFT_RESET);
	if (ret)
		return ret;
	ret = aw88271_write_profile(aw);
	if (ret)
		return ret;

	aw88271_sysctrl_bits(aw, AW88271_PWDN, false);
	usleep_range(2000, 2010);
	ret = aw88271_wait_sysst(aw, AW88271_IIS_CHECK);
	if (ret)
		goto fail;

	aw88271_sysctrl_bits(aw, AW88271_AMPPD, false);
	usleep_range(1000, 1050);
	ret = aw88271_wait_sysst(aw, AW88271_SYSST_CHECK);
	if (ret)
		goto fail;

	regmap_update_bits(aw->regmap, AW88271_REG_SYSCTRL, AW88271_I2STXEN, aw->txen_cfg);
	ret = aw88271_apply_volume(aw);
	if (ret)
		goto fail;
	aw88271_sysctrl_bits(aw, AW88271_ULS_HMUTE | AW88271_HMUTE, false);
	regmap_read(aw->regmap, AW88271_REG_SYSINT, &val);	/* clear interrupts */
	aw->powered = true;

	return 0;

fail:
	aw88271_power_off(aw);
	return ret;
}

static void aw88271_start_work(struct work_struct *work)
{
	struct aw88271 *aw = container_of(to_delayed_work(work), struct aw88271, start_work);
	int i, ret = -ENODEV;

	mutex_lock(&aw->lock);
	for (i = 0; i < AW88271_START_RETRIES && aw->fw_ok && !aw->powered; i++) {
		ret = aw88271_power_on(aw);
		if (!ret)
			break;
		msleep(AW88271_START_DELAY_MS);
	}
	if (ret)
		dev_err(aw->aw_pa->dev, "start failed (%d), amp stays off\n", ret);
	mutex_unlock(&aw->lock);
}

int aw88271_set_sp_active(struct aw88271 *aw, bool active)
{
	int ret;

	mutex_lock(&aw->lock);
	aw->sp_active = active;
	ret = aw88271_apply_volume(aw);
	mutex_unlock(&aw->lock);

	return ret;
}

static int aw88271_sp_notify(struct notifier_block *nb, unsigned long state, void *data)
{
	struct aw88271 *aw = container_of(nb, struct aw88271, sp_nb);

	aw88271_set_sp_active(aw, state == Q6APM_SP_ACTIVE);

	return NOTIFY_OK;
}

static void aw88271_sp_unregister(void *data)
{
	struct aw88271 *aw = data;

	q6apm_sp_notifier_unregister(&aw->sp_nb);
}

/*
 * Follow the ADSP speaker protection state when the platform reports it.
 * Without it, sp_active stays false and only the unprotected cap applies.
 */
static int aw88271_sp_register(struct aw88271 *aw)
{
	int ret;

	if (!IS_REACHABLE(CONFIG_SND_SOC_QDSP6_APM))
		return 0;

	aw->sp_nb.notifier_call = aw88271_sp_notify;
	ret = q6apm_sp_notifier_register(&aw->sp_nb);
	if (ret)
		return ret;

	return devm_add_action_or_reset(aw->aw_pa->dev, aw88271_sp_unregister, aw);
}

static int aw88271_playback_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (aw->fw_ok)
			schedule_delayed_work(&aw->start_work,
					      msecs_to_jiffies(AW88271_START_DELAY_MS));
		break;
	case SND_SOC_DAPM_POST_PMD:
		cancel_delayed_work_sync(&aw->start_work);
		mutex_lock(&aw->lock);
		aw88271_power_off(aw);
		mutex_unlock(&aw->lock);
		break;
	}

	return 0;
}

static int aw88271_capture_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);

	mutex_lock(&aw->lock);
	aw->vi_running = event == SND_SOC_DAPM_PRE_PMU;
	aw88271_apply_volume(aw);
	mutex_unlock(&aw->lock);

	return 0;
}

static const struct snd_soc_dapm_widget aw88271_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("AIF_RX", "Speaker_Playback", 0, SND_SOC_NOPM, 0, 0,
			      aw88271_playback_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("DAC Output"),
	SND_SOC_DAPM_AIF_OUT_E("AIF_TX", "Speaker_Capture", 0, SND_SOC_NOPM, 0, 0,
			       aw88271_capture_event,
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_INPUT("VI Input"),
};

static const struct snd_soc_dapm_route aw88271_audio_map[] = {
	{ "DAC Output", NULL, "AIF_RX" },
	{ "AIF_TX", NULL, "VI Input" },
};

static int aw88271_volume_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = aw->ctl_vol;

	return 0;
}

static int aw88271_volume_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);
	long value = ucontrol->value.integer.value[0];
	int ret;

	if (value < 0 || value > AW88271_VOL_CTL_MAX)
		return -EINVAL;

	mutex_lock(&aw->lock);
	if (aw->ctl_vol == value) {
		mutex_unlock(&aw->lock);
		return 0;
	}
	aw->ctl_vol = value;
	ret = aw88271_apply_volume(aw);
	mutex_unlock(&aw->lock);

	return ret ? ret : 1;
}

static const struct snd_kcontrol_new aw88271_controls[] = {
	SOC_SINGLE_EXT("PCM Playback Volume", SND_SOC_NOPM, 0, AW88271_VOL_CTL_MAX, 0,
		       aw88271_volume_get, aw88271_volume_put),
};

static int aw88271_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	/* The ACF programs the stock link: 48 kHz, 32-bit slots, 64 fs */
	if (params_rate(params) != 48000 || params_physical_width(params) != 32) {
		dev_err(dai->dev, "unsupported %u Hz / %u-bit link, need 48000 Hz / 32-bit\n",
			params_rate(params), params_physical_width(params));
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops aw88271_dai_ops = {
	.hw_params = aw88271_hw_params,
};

static struct snd_soc_dai_driver aw88271_dai[] = {
	{
		.name = "aw88271-aif",
		.playback = {
			.stream_name = "Speaker_Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "Speaker_Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &aw88271_dai_ops,
	},
};

static int aw88271_load_acf(struct aw88271 *aw)
{
	struct aw_device *aw_dev = aw->aw_pa;
	const struct firmware *fw;
	const char *fw_name;
	int prof, ret;

	ret = device_property_read_string(aw_dev->dev, "firmware-name", &fw_name);
	if (ret)
		return ret;

	ret = request_firmware(&fw, fw_name, aw_dev->dev);
	if (ret)
		return ret;

	aw->aw_cfg = devm_kzalloc(aw_dev->dev, struct_size(aw->aw_cfg, data, fw->size),
				  GFP_KERNEL);
	if (!aw->aw_cfg) {
		release_firmware(fw);
		return -ENOMEM;
	}
	aw->aw_cfg->len = fw->size;
	memcpy(aw->aw_cfg->data, fw->data, fw->size);
	release_firmware(fw);

	ret = aw88395_dev_load_acf_check(aw_dev, aw->aw_cfg);
	if (ret)
		return ret;
	ret = aw88395_dev_cfg_load(aw_dev, aw->aw_cfg);
	if (ret)
		return ret;

	prof = aw88271_find_profile(aw, "Music");
	if (prof < 0)
		return prof;
	aw_dev->prof_index = prof;
	aw_dev->prof_cur = prof;

	return 0;
}

static int aw88271_codec_probe(struct snd_soc_component *component)
{
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);
	int ret;

	mutex_lock(&aw->lock);
	ret = aw88271_load_acf(aw);
	if (!ret)
		ret = regmap_write(aw->regmap, AW88271_REG_ID, AW88271_SOFT_RESET);
	if (!ret)
		ret = aw88271_write_profile(aw);
	if (ret) {
		/* Fail closed: the card still registers, the amp never powers up */
		dev_err(aw->aw_pa->dev, "ACF/profile setup failed (%d): fail closed, amp stays off\n",
			ret);
		aw->fw_ok = false;
	} else {
		aw->fw_ok = true;
	}
	mutex_unlock(&aw->lock);

	return 0;
}

static void aw88271_codec_remove(struct snd_soc_component *component)
{
	struct aw88271 *aw = snd_soc_component_get_drvdata(component);

	cancel_delayed_work_sync(&aw->start_work);
	mutex_lock(&aw->lock);
	aw88271_power_off(aw);
	mutex_unlock(&aw->lock);
}

static const struct snd_soc_component_driver aw88271_component = {
	.probe = aw88271_codec_probe,
	.remove = aw88271_codec_remove,
	.controls = aw88271_controls,
	.num_controls = ARRAY_SIZE(aw88271_controls),
	.dapm_widgets = aw88271_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aw88271_dapm_widgets),
	.dapm_routes = aw88271_audio_map,
	.num_dapm_routes = ARRAY_SIZE(aw88271_audio_map),
};

static int aw88271_att_show(struct seq_file *s, void *unused)
{
	struct aw88271 *aw = s->private;

	mutex_lock(&aw->lock);
	seq_printf(s, "att=%u att_min=%u ctl=%u role=%s vi_running=%d sp_active=%d powered=%d fw_ok=%d\n",
		   aw88271_cur_att(aw),
		   aw88271_att_min(aw->role, aw->vi_running, aw->sp_active),
		   aw->ctl_vol, aw->role == AW88271_ROLE_SPEAKER ? "speaker" : "earpiece",
		   aw->vi_running, aw->sp_active, aw->powered, aw->fw_ok);
	mutex_unlock(&aw->lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(aw88271_att);

static void aw88271_debugfs_remove(void *data)
{
	debugfs_remove_recursive(data);
}

static int aw88271_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct aw_device *aw_dev;
	struct aw88271 *aw;
	const char *role;
	unsigned int chip_id;
	char name[32];
	u32 channel = 0;
	int ret;

	aw = devm_kzalloc(dev, sizeof(*aw), GFP_KERNEL);
	if (!aw)
		return -ENOMEM;
	aw_dev = devm_kzalloc(dev, sizeof(*aw_dev), GFP_KERNEL);
	if (!aw_dev)
		return -ENOMEM;

	mutex_init(&aw->lock);
	INIT_DELAYED_WORK(&aw->start_work, aw88271_start_work);
	i2c_set_clientdata(i2c, aw);

	ret = devm_regulator_get_enable_optional(dev, "dvdd");
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable dvdd\n");

	aw->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(aw->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(aw->reset_gpio), "failed to get reset gpio\n");
	if (aw->reset_gpio) {
		usleep_range(1000, 1100);
		gpiod_set_value_cansleep(aw->reset_gpio, 0);
		usleep_range(2000, 2100);
	}

	aw->regmap = devm_regmap_init_i2c(i2c, &aw88271_regmap_config);
	if (IS_ERR(aw->regmap))
		return dev_err_probe(dev, PTR_ERR(aw->regmap), "failed to init regmap\n");

	ret = regmap_read(aw->regmap, AW88271_REG_ID, &chip_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip id\n");
	if (chip_id != AW88271_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "unsupported chip id 0x%04x\n", chip_id);

	of_property_read_u32(dev->of_node, "awinic,audio-channel", &channel);
	aw->role = AW88271_ROLE_EARPIECE;
	if (!device_property_read_string(dev, "awinic,transducer", &role) &&
	    !strcmp(role, "speaker"))
		aw->role = AW88271_ROLE_SPEAKER;

	aw->aw_pa = aw_dev;
	aw_dev->i2c = i2c;
	aw_dev->regmap = aw->regmap;
	aw_dev->dev = dev;
	aw_dev->chip_id = AW88271_CHIP_ID;
	aw_dev->channel = channel;
	aw_dev->prof_info.prof_type = AW88395_DEV_NONE_TYPE_ID;

	/* Mute and power down until a stream starts */
	regmap_write(aw->regmap, AW88271_REG_ID, AW88271_SOFT_RESET);
	aw88271_power_off(aw);

	snprintf(name, sizeof(name), "aw88271-%02x", i2c->addr);
	aw->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("att", 0444, aw->debugfs, aw, &aw88271_att_fops);
	ret = devm_add_action_or_reset(dev, aw88271_debugfs_remove, aw->debugfs);
	if (ret)
		return ret;

	ret = aw88271_sp_register(aw);
	if (ret)
		return dev_err_probe(dev, ret, "failed to follow speaker protection state\n");

	dev_info(dev, "AW88271 (0x%04x), channel %u, %s\n", chip_id, channel,
		 aw->role == AW88271_ROLE_SPEAKER ? "speaker" : "earpiece");

	return devm_snd_soc_register_component(dev, &aw88271_component,
					       aw88271_dai, ARRAY_SIZE(aw88271_dai));
}

static const struct i2c_device_id aw88271_i2c_id[] = {
	{ .name = "aw88271" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw88271_i2c_id);

static const struct of_device_id aw88271_of_match[] = {
	{ .compatible = "awinic,aw88271" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw88271_of_match);

static struct i2c_driver aw88271_i2c_driver = {
	.driver = {
		.name = "aw88271",
		.of_match_table = aw88271_of_match,
	},
	.probe = aw88271_i2c_probe,
	.id_table = aw88271_i2c_id,
};
module_i2c_driver(aw88271_i2c_driver);

MODULE_DESCRIPTION("ASoC awinic AW88271 smart amplifier driver");
MODULE_LICENSE("GPL");
