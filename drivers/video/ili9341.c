// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <command.h>
#include <dm.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <mipi_display.h>
#include <video.h>
#include <spi.h>
#include <asm/gpio.h>

#define ILI9341_FRMCTR1		0xb1
#define ILI9341_DISCTRL		0xb6
#define ILI9341_ETMOD		0xb7

#define ILI9341_PWCTRL1		0xc0
#define ILI9341_PWCTRL2		0xc1
#define ILI9341_VMCTRL1		0xc5
#define ILI9341_VMCTRL2		0xc7
#define ILI9341_PWCTRLA		0xcb
#define ILI9341_PWCTRLB		0xcf

#define ILI9341_PGAMCTRL	0xe0
#define ILI9341_NGAMCTRL	0xe1
#define ILI9341_DTCTRLA		0xe8
#define ILI9341_DTCTRLB		0xea
#define ILI9341_PWRSEQ		0xed

#define ILI9341_EN3GAM		0xf2
#define ILI9341_PUMPCTRL	0xf7

#define ILI9341_MADCTL_BGR	BIT(3)
#define ILI9341_MADCTL_MV	BIT(5)
#define ILI9341_MADCTL_MX	BIT(6)
#define ILI9341_MADCTL_MY	BIT(7)

struct ili9341_priv {
	struct gpio_desc dc;
	bool late_probed;
};

static int ili9341_write(struct udevice *dev, uint8_t cmd,
			 const uint8_t *params, size_t params_len)
{
	struct ili9341_priv *priv = dev_get_priv(dev);
	unsigned long flags = SPI_XFER_BEGIN;
	int ret = 0;

	ret = dm_spi_claim_bus(dev);
	if (ret)
		return ret;

	if (params_len == 0)
		flags |= SPI_XFER_END;

	dm_gpio_set_value(&priv->dc, false);
	ret = dm_spi_xfer(dev, 8, &cmd, NULL, flags);
	if (ret < 0)
		goto out;

	if (params_len > 0) {
		dm_gpio_set_value(&priv->dc, true);
		ret = dm_spi_xfer(dev, params_len * 8, params, NULL, SPI_XFER_END);
		if (ret < 0)
			goto out;
	}

out:
	dm_spi_release_bus(dev);

	return ret;
}

#define ili9341_command(dev, cmd, seq...)			\
({								\
	u8 params[] = { seq };					\
	ili9341_write(dev, cmd, params, ARRAY_SIZE(params));	\
})

static struct udevice *ili9341 = NULL;

static int ili9341_init(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	uint8_t addr_mode;
	int ret;

dev_err(dev, "%s() :%d\n", __func__, __LINE__);
	ret = ili9341_command(dev, MIPI_DCS_SOFT_RESET);
	if (ret < 0) {
		dev_err(dev, "Error resetting display: %d\n", ret);
		return ret;
	}
dev_err(dev, "%s() :%d\n", __func__, __LINE__);

	mdelay(120);

	ili9341_command(dev, MIPI_DCS_SET_DISPLAY_OFF);

	ili9341_command(dev, ILI9341_PWCTRLB, 0x00, 0xc1, 0x30);
	ili9341_command(dev, ILI9341_PWRSEQ, 0x64, 0x03, 0x12, 0x81);
	ili9341_command(dev, ILI9341_DTCTRLA, 0x85, 0x00, 0x78);
	ili9341_command(dev, ILI9341_PWCTRLA, 0x39, 0x2c, 0x00, 0x34, 0x02);
	ili9341_command(dev, ILI9341_PUMPCTRL, 0x20);
	ili9341_command(dev, ILI9341_DTCTRLB, 0x00, 0x00);

	/* Power Control */
	ili9341_command(dev, ILI9341_PWCTRL1, 0x23);
	ili9341_command(dev, ILI9341_PWCTRL2, 0x10);
	/* VCOM */
	ili9341_command(dev, ILI9341_VMCTRL1, 0x3e, 0x28);
	ili9341_command(dev, ILI9341_VMCTRL2, 0x86);

	/* Memory Access Control */
	ili9341_command(dev, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);

	/* Frame Rate */
	ili9341_command(dev, ILI9341_FRMCTR1, 0x00, 0x1b);

	/* Gamma */
	ili9341_command(dev, ILI9341_EN3GAM, 0x00);
	ili9341_command(dev, MIPI_DCS_SET_GAMMA_CURVE, 0x01);
	ili9341_command(dev, ILI9341_PGAMCTRL,
			0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1,
			0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00);
	ili9341_command(dev, ILI9341_NGAMCTRL,
			0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1,
			0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f);

	/* DDRAM */
	ili9341_command(dev, ILI9341_ETMOD, 0x07);

	/* Display */
	ili9341_command(dev, ILI9341_DISCTRL, 0x08, 0x82, 0x27, 0x00);
	ili9341_command(dev, MIPI_DCS_EXIT_SLEEP_MODE);
	mdelay(100);

	ili9341_command(dev, MIPI_DCS_SET_DISPLAY_ON);
	mdelay(100);

	switch (uc_priv->rot) {
	default:
		addr_mode = ILI9341_MADCTL_MX;
		break;
	case 90:
		addr_mode = ILI9341_MADCTL_MV;
		break;
	case 180:
		addr_mode = ILI9341_MADCTL_MY;
		break;
	case 270:
		addr_mode = ILI9341_MADCTL_MV | ILI9341_MADCTL_MY |
			    ILI9341_MADCTL_MX;
		break;
	}
	addr_mode |= ILI9341_MADCTL_BGR;
	ili9341_command(dev, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	return 0;
}

static int ili9341_late_probe(struct udevice *dev)
{
	struct ili9341_priv *priv = dev_get_priv(dev);
	struct gpio_desc reset;
	int ret;

	if (priv->late_probed)
		return 0;

	ret = gpio_request_by_name(dev, "reset-gpios", 0, &reset,
				   GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "%s: Warning: cannot get reset GPIO: ret=%d\n",
			__func__, ret);
		if (ret != -ENOENT)
			return log_ret(ret);
	}

	ret = gpio_request_by_name(dev, "dc-gpios", 0, &priv->dc,
				   GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "%s: Warning: cannot get D/C GPIO: ret=%d\n",
			__func__, ret);
		if (ret != -ENOENT)
			return log_ret(ret);
	}

	dm_gpio_set_value(&reset, 1);
	ili9341_init(dev);

	priv->late_probed = true;

	return 0;
}

static int ili9341_sync(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	uint8_t *fb = uc_priv->fb;

	ili9341_command(dev, MIPI_DCS_SET_COLUMN_ADDRESS, 0, 0, uc_priv->xsize >> 8, uc_priv->xsize & 0xff);
	ili9341_command(dev, MIPI_DCS_SET_PAGE_ADDRESS, 0, 0, uc_priv->ysize >> 8, uc_priv->ysize & 0xff);
	ili9341_write(dev, MIPI_DCS_WRITE_MEMORY_START, fb, uc_priv->fb_size);

	return 0;
}

static int do_ili9341(cmd_tbl_t *cmdtp, int flag, int argc,
		      char *const argv[])
{
	struct udevice *dev = ili9341;
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	char *s;

	if (!dev) {
		printf("ERROR: ili9341 device not probed\n");
		return 1;
	}

	ili9341_late_probe(dev);

	s = env_get("bmp_addr");
	if (s) {
		ulong bmp_addr = simple_strtol(s, NULL, 16);
		printf("Displaying BMP @%lx\n", bmp_addr);
		uc_priv->fb = (uint8_t *) bmp_addr + 0x1000000;

		video_bmp_display(dev, bmp_addr, 0, 0, false);
		ili9341_sync(dev);
	}

	return 0;
}

U_BOOT_CMD(ili9341, 1, 1, do_ili9341, "ili9341", "");

static int ili9341_probe(struct udevice *dev)
{
	struct video_uc_platdata *plat = dev_get_uclass_platdata(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct ili9341_priv *priv = dev_get_priv(dev);

	uc_priv->xsize = 320;
	uc_priv->ysize = 240;
	uc_priv->rot = 270;
	uc_priv->bpix = VIDEO_BPP16;

	plat->size = uc_priv->xsize * uc_priv->ysize * 2;

	dev->flags |= DM_FLAG_BOUND;

	ili9341 = dev;
	priv->late_probed = false;

	return 0;
}

static const struct udevice_id ili9341_ids[] = {
	{ .compatible = "ilitek,ili9341" },
	{ }
};

U_BOOT_DRIVER(ili9341_video) = {
	.name	= "ili9341_video",
	.id	= UCLASS_VIDEO,
	.of_match = ili9341_ids,
	.probe	= ili9341_probe,
	.priv_auto_alloc_size = sizeof(struct ili9341_priv),
};
