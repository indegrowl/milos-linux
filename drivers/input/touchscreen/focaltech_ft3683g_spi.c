// SPDX-License-Identifier: GPL-2.0
/*
 * FocalTech FT3683G SPI touchscreen driver
 *
 * Minimal driver for the FT3683G (FT5672 core) used in the Nothing
 * Phone (4a). The controller runs firmware from its own flash; this driver
 * only powers it, checks that the application firmware answers and reads
 * touch reports. It never writes to the controller: no register writes, no
 * boot ROM or flash commands.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include "focaltech_ft3683g.h"

#define FT3683G_CHIP_ID_H	0x56
#define FT3683G_CHIP_ID_L	0x72
#define FT3683G_ID_TRIES	5
#define FT3683G_ID_INTERVAL_MS	200
#define FT3683G_READ_TRIES	20
#define FT3683G_BUF_LEN		(FT3683G_TOUCH_LEN + FT3683G_READ_OVERHEAD)

struct ft3683g {
	struct spi_device *spi;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct regulator *vci;
	struct regulator *vddio;
	struct touchscreen_properties prop;
	u8 *tx;
	u8 *rx;
	u8 data[FT3683G_TOUCH_LEN];
	struct ft3683g_contact contacts[FT3683G_MAX_CONTACTS];
};

static int ft3683g_read(struct ft3683g *ts, u8 reg, u8 *data, u16 len)
{
	struct spi_transfer xfer = {
		.tx_buf = ts->tx,
		.rx_buf = ts->rx,
	};
	int error = -EIO;
	int i;

	for (i = 0; i < FT3683G_READ_TRIES; i++) {
		xfer.len = ft3683g_build_read(ts->tx, reg, len);
		error = spi_sync_transfer(ts->spi, &xfer, 1);
		/* The controller needs CS high for 150 us between frames */
		usleep_range(150, 200);
		if (error)
			continue;

		error = ft3683g_check_read(ts->rx, len);
		if (!error) {
			memcpy(data, ts->rx + FT3683G_READ_DATA_OFF, len);
			return 0;
		}
	}

	return error;
}

static int ft3683g_read_reg(struct ft3683g *ts, u8 reg, u8 *val)
{
	return ft3683g_read(ts, reg, val, 1);
}

static int ft3683g_power_on(struct ft3683g *ts)
{
	int error;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	usleep_range(2000, 3000);

	error = regulator_enable(ts->vci);
	if (error)
		return error;

	error = regulator_enable(ts->vddio);
	if (error) {
		regulator_disable(ts->vci);
		return error;
	}

	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ts->reset_gpio, 0);
	msleep(200);

	return 0;
}

static void ft3683g_power_off(void *data)
{
	struct ft3683g *ts = data;

	gpiod_set_value_cansleep(ts->reset_gpio, 1);
	usleep_range(2000, 3000);
	regulator_disable(ts->vci);
	usleep_range(200, 300);
	regulator_disable(ts->vddio);
}

static int ft3683g_check_chip_id(struct ft3683g *ts)
{
	struct device *dev = &ts->spi->dev;
	u8 id_h = 0, id_l = 0, fw = 0, vendor = 0;
	int i;

	for (i = 0; i < FT3683G_ID_TRIES; i++) {
		if (i)
			msleep(FT3683G_ID_INTERVAL_MS);

		if (ft3683g_read_reg(ts, FT3683G_REG_CHIP_ID, &id_h) ||
		    ft3683g_read_reg(ts, FT3683G_REG_CHIP_ID2, &id_l))
			continue;

		if (id_h == FT3683G_CHIP_ID_H && id_l == FT3683G_CHIP_ID_L)
			break;
	}

	if (i == FT3683G_ID_TRIES) {
		dev_err(dev, "chip id %02x%02x, expected %02x%02x\n",
			id_h, id_l, FT3683G_CHIP_ID_H, FT3683G_CHIP_ID_L);
		return -ENODEV;
	}

	/* Informational only */
	ft3683g_read_reg(ts, FT3683G_REG_FW_VER, &fw);
	ft3683g_read_reg(ts, FT3683G_REG_VENDOR, &vendor);
	dev_info(dev, "FT3683G id 0x%02x%02x fw 0x%02x vendor 0x%02x\n",
		 id_h, id_l, fw, vendor);

	return 0;
}

static void ft3683g_report(struct ft3683g *ts, unsigned int n)
{
	struct input_dev *input = ts->input;
	unsigned int i;

	for (i = 0; i < n; i++) {
		const struct ft3683g_contact *c = &ts->contacts[i];

		input_mt_slot(input, c->id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, c->down);
		if (!c->down)
			continue;

		touchscreen_report_pos(input, &ts->prop, c->x, c->y, true);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, c->major);
	}

	/* Releases slots without a down record and emulates BTN_TOUCH */
	input_mt_sync_frame(input);
	input_sync(input);
}

static irqreturn_t ft3683g_irq(int irq, void *dev_id)
{
	struct ft3683g *ts = dev_id;
	unsigned int n;
	int ret;

	ret = ft3683g_read(ts, FT3683G_REG_TOUCH, ts->data, FT3683G_TOUCH_LEN);
	if (ret) {
		dev_err_ratelimited(&ts->spi->dev, "touch read failed: %d\n", ret);
		return IRQ_HANDLED;
	}

	ret = ft3683g_parse_touch(ts->data, ts->contacts, &n);
	if (ret == FT3683G_PACKET_TOUCH)
		dev_info_once(&ts->spi->dev, "report format %u\n",
			      ts->data[1] >> 4);

	if (ret == FT3683G_PACKET_TOUCH || ret == FT3683G_PACKET_FW_INIT)
		ft3683g_report(ts, n);

	return IRQ_HANDLED;
}

static int ft3683g_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ft3683g *ts;
	int error;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->spi = spi;
	spi_set_drvdata(spi, ts);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	error = spi_setup(spi);
	if (error)
		return dev_err_probe(dev, error, "Failed to setup SPI\n");

	/* Separate allocations so the SPI controller can DMA them */
	ts->tx = devm_kzalloc(dev, FT3683G_BUF_LEN, GFP_KERNEL);
	ts->rx = devm_kzalloc(dev, FT3683G_BUF_LEN, GFP_KERNEL);
	if (!ts->tx || !ts->rx)
		return -ENOMEM;

	ts->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ts->vci))
		return dev_err_probe(dev, PTR_ERR(ts->vci), "Failed to get vci\n");

	ts->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ts->vddio))
		return dev_err_probe(dev, PTR_ERR(ts->vddio), "Failed to get vddio\n");

	ts->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->reset_gpio),
				     "Failed to request reset GPIO\n");

	error = ft3683g_power_on(ts);
	if (error)
		return dev_err_probe(dev, error, "Failed to power on\n");

	error = devm_add_action_or_reset(dev, ft3683g_power_off, ts);
	if (error)
		return error;

	error = ft3683g_check_chip_id(ts);
	if (error)
		return error;

	ts->input = devm_input_allocate_device(dev);
	if (!ts->input)
		return -ENOMEM;

	ts->input->name = "FocalTech FT3683G";
	ts->input->id.bustype = BUS_SPI;

	input_set_capability(ts->input, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(ts->input, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	touchscreen_parse_properties(ts->input, true, &ts->prop);

	error = input_mt_init_slots(ts->input, FT3683G_MAX_CONTACTS,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return dev_err_probe(dev, error, "Failed to initialize MT slots\n");

	error = input_register_device(ts->input);
	if (error)
		return error;

	error = devm_request_threaded_irq(dev, spi->irq, NULL, ft3683g_irq,
					  IRQF_ONESHOT, dev_name(dev), ts);
	if (error)
		return dev_err_probe(dev, error, "Failed to request IRQ\n");

	return 0;
}

static const struct of_device_id ft3683g_of_match[] = {
	{ .compatible = "focaltech,ft3683g" },
	{ }
};
MODULE_DEVICE_TABLE(of, ft3683g_of_match);

static const struct spi_device_id ft3683g_spi_id[] = {
	{ .name = "ft3683g" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ft3683g_spi_id);

static struct spi_driver ft3683g_driver = {
	.driver = {
		.name = "focaltech-ft3683g",
		.of_match_table = ft3683g_of_match,
	},
	.probe = ft3683g_probe,
	.id_table = ft3683g_spi_id,
};
module_spi_driver(ft3683g_driver);

MODULE_IMPORT_NS("FT3683G");
MODULE_DESCRIPTION("FocalTech FT3683G SPI touchscreen driver");
MODULE_LICENSE("GPL");
