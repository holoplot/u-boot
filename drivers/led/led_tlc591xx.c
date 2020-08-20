// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <i2c.h>
#include <led.h>
#include <dm/lists.h>

#define TLC591XX_MODE1		0x00
#define TLC591XX_LEDOUT0	0x0c
#define TLC591XX_LEDOUT1	0x0d

struct tlc591xx_led_priv {
	int index;
};

static enum led_state_t tlc591xx_led_get_state(struct udevice *dev)
{
	struct tlc591xx_led_priv *priv = dev_get_priv(dev);
	struct udevice *parent_dev = dev_get_parent(dev);
	int shift, ret;
	uint8_t val;
	uint reg;

	reg = (priv->index < 4) ? TLC591XX_LEDOUT0 : TLC591XX_LEDOUT1;
	shift = (priv->index % 4) * 2;

	ret = dm_i2c_read(parent_dev, reg, &val, 1);
	if (ret < 0) {
		dev_err(parent_dev, "Cannot read from i2c device: %d\n", ret);
		return LEDST_OFF;
	}

	return (val & (1 << shift)) ? LEDST_ON : LEDST_OFF;
}

static int tlc591xx_led_set_state(struct udevice *dev, enum led_state_t state)
{
	struct tlc591xx_led_priv *priv = dev_get_priv(dev);
	struct udevice *parent_dev = dev_get_parent(dev);
	int shift, ret;
	uint8_t val;
	uint reg;

	reg = (priv->index < 4) ? TLC591XX_LEDOUT0 : TLC591XX_LEDOUT1;
	shift = (priv->index % 4) * 2;

	ret = dm_i2c_read(parent_dev, reg, &val, 1);
	if (ret < 0) {
		dev_err(parent_dev, "Cannot read from i2c device: %d\n", ret);
		return ret;
	}

	/* disable PWM dimming */
	val &= ~(2 << shift);

	switch (state) {
	case LEDST_OFF:
		val &= ~(1 << shift);
		break;
	case LEDST_ON:
		val |= 1 << shift;
		break;
	case LEDST_TOGGLE:
		val ^= 1 << shift;
		break;
	default:
		return -ENOSYS;
	}

	return dm_i2c_write(parent_dev, reg, &val, 1);
}

static const struct led_ops tlc591xx_led_ops = {
	.get_state = tlc591xx_led_get_state,
	.set_state = tlc591xx_led_set_state,
};

static int tlc591xx_led_probe(struct udevice *dev)
{
	struct led_uc_plat *uc_plat = dev_get_uclass_platdata(dev);
	struct tlc591xx_led_priv *priv = dev_get_priv(dev);

	/* Top-level LED node */
	if (!uc_plat->label) {
		uint8_t val = 0;

		/* Oscillator on */
		return dm_i2c_write(dev, TLC591XX_MODE1, &val, 1);
	}

	priv->index = dev_read_u32_default(dev, "reg", 0);
	if (priv->index >= 8)
		return -EINVAL;

	return 0;
}

static int tlc591xx_led_bind(struct udevice *parent)
{
	ofnode node;

	dev_for_each_subnode(node, parent) {
		struct led_uc_plat *uc_plat;
		struct udevice *dev;
		const char *label;
		int ret;

		label = ofnode_read_string(node, "label");
		if (!label) {
			debug("%s: node %s has no label\n", __func__,
			      ofnode_get_name(node));
			return -EINVAL;
		}

		ret = device_bind_driver_to_node(parent, "tlc591xx-led",
						 ofnode_get_name(node),
						 node, &dev);
		if (ret)
			return ret;

		uc_plat = dev_get_uclass_platdata(dev);
		uc_plat->label = label;
	}

	return 0;
}

static const struct udevice_id tlc591xx_led_ids[] = {
	{ .compatible = "ti,tlc59108" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(tlc591xx_led) = {
	.name = "tlc591xx-led",
	.id = UCLASS_LED,
	.of_match = tlc591xx_led_ids,
	.ops = &tlc591xx_led_ops,
	.bind = tlc591xx_led_bind,
	.probe = tlc591xx_led_probe,
	.priv_auto_alloc_size = sizeof(struct tlc591xx_led_priv),
};
