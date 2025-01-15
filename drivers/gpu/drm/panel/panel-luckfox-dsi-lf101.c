// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2024 Dimitar Lazarov
 *
 */

#define DEBUG 1

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#define LF_DSI_DRIVER_NAME "panel-lf-dsi-lf101"

#define USE_POWER_REGULATOR 0
#define USE_ORIENTATION 0
#define PANEL_DCS_IN_PREPARE 1

#define INIT_CMD_LEN		2

struct LCM_init_cmd {
	u8 data[INIT_CMD_LEN];
};

struct lf_panel_data {
	const struct drm_display_mode *mode;
	int lanes;
	unsigned long mode_flags;
	const struct LCM_init_cmd *init_cmds;
	int init_cmds_count;
};

struct lf_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;
	const struct lf_panel_data *data;
#if USE_POWER_REGULATOR
	struct regulator *power;
#endif
	struct gpio_desc *reset;
#if USE_ORIENTATION
	enum drm_panel_orientation orientation;
#endif
	bool prepared;
	bool enabled;
};

/* 10.1inch 800x1280
 * https://www.luckfox.com/EN-LF101-8001280-AMA
 */
static const struct drm_display_mode LF101_8001280_AMA_mode = {
	.clock =        69907, // 70000,
	.hdisplay =     800,                    // hactive
	.hsync_start =  800 + 40,               // hactive + hfp
	.hsync_end =    800 + 40 + 20,          // hactive + hfp + hsync
	.htotal =       800 + 40 + 20 + 20,     // hactive + hfp + hsync + hbp
	.vdisplay =     1280,                   // vactive
	.vsync_start =  1280 + 20,              // vactive + vfp
	.vsync_end =    1280 + 20 + 4,          // vactive + vfp + vsync
	.vtotal =       1280 + 20 + 4 + 20,     // vactive + vfp + vsync + fbp
    .width_mm =     135,
    .height_mm =    216,
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct LCM_init_cmd LF101_8001280_AMA_init_cmds[] = {
	{.data = {0xE0,0x00}},
	{.data = {0xE1,0x93}},
	{.data = {0xE2,0x65}},
	{.data = {0xE3,0xF8}},
	{.data = {0x80,0x03}},

	{.data = {0xE0,0x01}},
	{.data = {0x00,0x00}},
	{.data = {0x01,0x3B}},
	{.data = {0x0C,0x74}},
	{.data = {0x17,0x00}},
	{.data = {0x18,0xAF}},
	{.data = {0x19,0x00}},
	{.data = {0x1A,0x00}},
	{.data = {0x1B,0xAF}},
	{.data = {0x1C,0x00}},
	{.data = {0x35,0x26}},
	{.data = {0x37,0x09}},
	{.data = {0x38,0x04}},
	{.data = {0x39,0x00}},
	{.data = {0x3A,0x01}},
	{.data = {0x3C,0x78}},
	{.data = {0x3D,0xFF}},
	{.data = {0x3E,0xFF}},
	{.data = {0x3F,0x7F}},
	{.data = {0x40,0x06}},
	{.data = {0x41,0xA0}},
	{.data = {0x42,0x81}},
	{.data = {0x43,0x14}},
	{.data = {0x44,0x23}},
	{.data = {0x45,0x28}},
	{.data = {0x55,0x02}},
	{.data = {0x57,0x69}},
	{.data = {0x59,0x0A}},
	{.data = {0x5A,0x2A}},
	{.data = {0x5B,0x17}},
	{.data = {0x5D,0x7F}},
	{.data = {0x5E,0x6B}},
	{.data = {0x5F,0x5C}},
	{.data = {0x60,0x4F}},
	{.data = {0x61,0x4D}},
	{.data = {0x62,0x3F}},
	{.data = {0x63,0x42}},
	{.data = {0x64,0x2B}},
	{.data = {0x65,0x44}},
	{.data = {0x66,0x43}},
	{.data = {0x67,0x43}},
	{.data = {0x68,0x63}},
	{.data = {0x69,0x52}},
	{.data = {0x6A,0x5A}},
	{.data = {0x6B,0x4F}},
	{.data = {0x6C,0x4E}},
	{.data = {0x6D,0x20}},
	{.data = {0x6E,0x0F}},
	{.data = {0x6F,0x00}},
	{.data = {0x70,0x7F}},
	{.data = {0x71,0x6B}},
	{.data = {0x72,0x5C}},
	{.data = {0x73,0x4F}},
	{.data = {0x74,0x4D}},
	{.data = {0x75,0x3F}},
	{.data = {0x76,0x42}},
	{.data = {0x77,0x2B}},
	{.data = {0x78,0x44}},
	{.data = {0x79,0x43}},
	{.data = {0x7A,0x43}},
	{.data = {0x7B,0x63}},
	{.data = {0x7C,0x52}},
	{.data = {0x7D,0x5A}},
	{.data = {0x7E,0x4F}},
	{.data = {0x7F,0x4E}},
	{.data = {0x80,0x20}},
	{.data = {0x81,0x0F}},
	{.data = {0x82,0x00}},

	{.data = {0xE0,0x02}},
	{.data = {0x00,0x02}},
	{.data = {0x01,0x02}},
	{.data = {0x02,0x00}},
	{.data = {0x03,0x00}},
	{.data = {0x04,0x1E}},
	{.data = {0x05,0x1E}},
	{.data = {0x06,0x1F}},
	{.data = {0x07,0x1F}},
	{.data = {0x08,0x1F}},
	{.data = {0x09,0x17}},
	{.data = {0x0A,0x17}},
	{.data = {0x0B,0x37}},
	{.data = {0x0C,0x37}},
	{.data = {0x0D,0x47}},
	{.data = {0x0E,0x47}},
	{.data = {0x0F,0x45}},
	{.data = {0x10,0x45}},
	{.data = {0x11,0x4B}},
	{.data = {0x12,0x4B}},
	{.data = {0x13,0x49}},
	{.data = {0x14,0x49}},
	{.data = {0x15,0x1F}},

	{.data = {0x16,0x01}},
	{.data = {0x17,0x01}},
	{.data = {0x18,0x00}},
	{.data = {0x19,0x00}},
	{.data = {0x1A,0x1E}},
	{.data = {0x1B,0x1E}},
	{.data = {0x1C,0x1F}},
	{.data = {0x1D,0x1F}},
	{.data = {0x1E,0x1F}},
	{.data = {0x1F,0x17}},
	{.data = {0x20,0x17}},
	{.data = {0x21,0x37}},
	{.data = {0x22,0x37}},
	{.data = {0x23,0x46}},
	{.data = {0x24,0x46}},
	{.data = {0x25,0x44}},
	{.data = {0x26,0x44}},
	{.data = {0x27,0x4A}},
	{.data = {0x28,0x4A}},
	{.data = {0x29,0x48}},
	{.data = {0x2A,0x48}},
	{.data = {0x2B,0x1F}},

	{.data = {0x2C,0x01}},
	{.data = {0x2D,0x01}},
	{.data = {0x2E,0x00}},
	{.data = {0x2F,0x00}},
	{.data = {0x30,0x1F}},
	{.data = {0x31,0x1F}},
	{.data = {0x32,0x1E}},
	{.data = {0x33,0x1E}},
	{.data = {0x34,0x1F}},
	{.data = {0x35,0x17}},
	{.data = {0x36,0x17}},
	{.data = {0x37,0x37}},
	{.data = {0x38,0x37}},
	{.data = {0x39,0x08}},
	{.data = {0x3A,0x08}},
	{.data = {0x3B,0x0A}},
	{.data = {0x3C,0x0A}},
	{.data = {0x3D,0x04}},
	{.data = {0x3E,0x04}},
	{.data = {0x3F,0x06}},
	{.data = {0x40,0x06}},
	{.data = {0x41,0x1F}},

	{.data = {0x42,0x02}},
	{.data = {0x43,0x02}},
	{.data = {0x44,0x00}},
	{.data = {0x45,0x00}},
	{.data = {0x46,0x1F}},
	{.data = {0x47,0x1F}},
	{.data = {0x48,0x1E}},
	{.data = {0x49,0x1E}},
	{.data = {0x4A,0x1F}},
	{.data = {0x4B,0x17}},
	{.data = {0x4C,0x17}},
	{.data = {0x4D,0x37}},
	{.data = {0x4E,0x37}},
	{.data = {0x4F,0x09}},
	{.data = {0x50,0x09}},
	{.data = {0x51,0x0B}},
	{.data = {0x52,0x0B}},
	{.data = {0x53,0x05}},
	{.data = {0x54,0x05}},
	{.data = {0x55,0x07}},
	{.data = {0x56,0x07}},
	{.data = {0x57,0x1F}},

	{.data = {0x58,0x40}},
	{.data = {0x5B,0x30}},
	{.data = {0x5C,0x16}},
	{.data = {0x5D,0x34}},
	{.data = {0x5E,0x05}},
	{.data = {0x5F,0x02}},
	{.data = {0x63,0x00}},
	{.data = {0x64,0x6A}},
	{.data = {0x67,0x73}},
	{.data = {0x68,0x1D}},
	{.data = {0x69,0x08}},
	{.data = {0x6A,0x6A}},
	{.data = {0x6B,0x08}},

	{.data = {0x6C,0x00}},
	{.data = {0x6D,0x00}},
	{.data = {0x6E,0x00}},
	{.data = {0x6F,0x88}},

	{.data = {0x75,0xFF}},
	{.data = {0x77,0xDD}},
	{.data = {0x78,0x3F}},
	{.data = {0x79,0x15}},
	{.data = {0x7A,0x17}},
	{.data = {0x7D,0x14}},
	{.data = {0x7E,0x82}},

	{.data = {0xE0,0x04}},
	{.data = {0x00,0x0E}},
	{.data = {0x02,0xB3}},
	{.data = {0x09,0x61}},
	{.data = {0x0E,0x48}},

	{.data = {0xE0,0x00}},
	{.data = {0xE6,0x02}},
	{.data = {0xE7,0x0C}},
	{.data = {0x11,0x00}},
};

static const struct lf_panel_data LF101_8001280_AMA_data = {
	.mode = &LF101_8001280_AMA_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_NO_EOT_PACKET, // | MIPI_DSI_MODE_LPM,
	.init_cmds = LF101_8001280_AMA_init_cmds,
	.init_cmds_count = ARRAY_SIZE(LF101_8001280_AMA_init_cmds),
};

static int lf_panel_set_init_cmds(struct mipi_dsi_device *dsi, const struct LCM_init_cmd *init_cmds, int init_cmds_count)
{
	pr_debug("lf_panel_set_init_cmds\n");

	int i;
	int ret;

	for (i = 0; i < init_cmds_count; i++) {
		const struct LCM_init_cmd *cmd = &init_cmds[i];
		pr_debug("lf_panel_set_init_cmds: cmd: 0x%02X, data: 0x%02X\n", cmd->data[0], cmd->data[1]);
		ret = mipi_dsi_dcs_write_buffer(dsi, cmd->data, INIT_CMD_LEN);
		if (ret < 0) {
			dev_err(&dsi->dev, "failed to write command: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static struct lf_panel *to_lf_panel(struct drm_panel *panel)
{
	return container_of(panel, struct lf_panel, base);
}

static int lf_panel_disable(struct drm_panel *panel)
{
	pr_debug("lf_panel_disable\n");

	struct lf_panel *lfp = to_lf_panel(panel);
	if (!lfp->enabled)
		return 0;

#if !PANEL_DCS_IN_PREPARE
	struct mipi_dsi_device *dsi = lfp->dsi;
	int ret;
	//dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}

	/* Enter sleep mode */
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}
#endif

	lfp->enabled = false;
	return 0;
}

static int lf_panel_unprepare(struct drm_panel *panel)
{
	pr_debug("lf_panel_unprepare\n");

	struct lf_panel *lfp = to_lf_panel(panel);
	if (!lfp->prepared)
		return 0;

#if PANEL_DCS_IN_PREPARE || USE_POWER_REGULATOR
	struct mipi_dsi_device *dsi = lfp->dsi;
	int ret;
#endif

#if PANEL_DCS_IN_PREPARE
	pr_debug("lf_panel_unprepare: set display off\n");
	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}

	/* Enter sleep mode */
	pr_debug("lf_panel_unprepare: enter sleep mode\n");
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}
#endif

	gpiod_set_value(lfp->reset, 1);
	msleep(120);

#if USE_POWER_REGULATOR
	ret = regulator_disable(lfp->power);
	if (ret < 0)
		dev_err(&dsi->dev, "regulator disable failed, %d\n", ret);
#endif

	lfp->prepared = false;
	return 0;
}

static int lf_panel_prepare(struct drm_panel *panel)
{
	pr_debug("lf_panel_prepare\n");

	struct lf_panel *lfp = to_lf_panel(panel);
	if (lfp->prepared)
		return 0;

#if PANEL_DCS_IN_PREPARE || USE_POWER_REGULATOR
	struct mipi_dsi_device *dsi = lfp->dsi;
	int ret;
#endif

#if USE_POWER_REGULATOR
	/* Power the panel */
	ret = regulator_enable(lfp->power);
	if (ret)
		return ret;
	msleep(5);
#endif

	/* Reset the panel */
	gpiod_set_value(lfp->reset, 1);
	msleep(5);

	gpiod_set_value(lfp->reset, 0);
	msleep(10);

	gpiod_set_value(lfp->reset, 1);
	msleep(120);

#if PANEL_DCS_IN_PREPARE
	/* Initialize the panel */
	msleep(10);
	ret = lf_panel_set_init_cmds(dsi, lfp->data->init_cmds, lfp->data->init_cmds_count);
	if (ret) {
		dev_err(&dsi->dev, "failed to set init cmds (%d)\n", ret);
		goto poweroff;
	}
	msleep(120);

	/* Exit sleep mode and power on */
	pr_debug("lf_panel_prepare: exit sleep mode\n");
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to exit sleep mode (%d)\n", ret);
		goto poweroff;
	}

	pr_debug("lf_panel_prepare: set display on\n");
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display on (%d)\n", ret);
		goto poweroff;
	}
#endif

	lfp->prepared = true;
	return 0;

#if PANEL_DCS_IN_PREPARE
poweroff:
#endif
#if USE_POWER_REGULATOR
	ret = regulator_disable(lfp->power);
	if (ret < 0)
		dev_err(&dsi->dev, "regulator disable failed, %d\n", ret);
#endif
#if PANEL_DCS_IN_PREPARE
	return ret;	
#endif
}

static int lf_panel_enable(struct drm_panel *panel)
{
	pr_debug("lf_panel_enable\n");

	struct lf_panel *lfp = to_lf_panel(panel);
	if (lfp->enabled)
		return 0;

#if !PANEL_DCS_IN_PREPARE
	struct mipi_dsi_device *dsi = lfp->dsi;
	int ret;
	//dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* Initialize the panel */
	msleep(10);
	ret = lf_panel_set_init_cmds(dsi, lfp->data->init_cmds, lfp->data->init_cmds_count);
	if (ret) {
		dev_err(&dsi->dev, "failed to set init cmds (%d)\n", ret);
		return ret;
	}
	msleep(120);

	/* Exit sleep mode and power on */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to exit sleep mode (%d)\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display on (%d)\n", ret);
		return ret;
	}
#endif

	lfp->enabled = true;
	return 0;
}

static int lf_panel_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	pr_debug("lf_panel_get_modes\n");

	struct lf_panel *lfp = to_lf_panel(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, lfp->data->mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			lfp->data->mode->hdisplay,
			lfp->data->mode->vdisplay,
			drm_mode_vrefresh(lfp->data->mode));
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

#if USE_ORIENTATION
	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, lfp->orientation);
#endif

	return 1;
}

#if USE_ORIENTATION
static enum drm_panel_orientation lf_panel_get_orientation(struct drm_panel *panel)
{
	struct lf_panel *lfp = to_lf_panel(panel);

	return lfp->orientation;
}
#endif

static const struct drm_panel_funcs lf_panel_funcs = {
	.disable = lf_panel_disable,
	.unprepare = lf_panel_unprepare,
	.prepare = lf_panel_prepare,
	.enable = lf_panel_enable,
	.get_modes = lf_panel_get_modes,
#if USE_ORIENTATION
	.get_orientation = lf_panel_get_orientation,
#endif
};

static int lf_panel_probe(struct mipi_dsi_device *dsi)
{
	pr_debug("lf_panel_probe\n");

	struct device *dev = &dsi->dev;
	struct lf_panel *lfp;
	const struct lf_panel_data *_lf_panel_data;
	int ret;

	lfp = devm_kzalloc(dev, sizeof(*lfp), GFP_KERNEL);
	if (!lfp)
		return -ENOMEM;

	pr_debug("lf_panel_probe: getting panel data\n");
	_lf_panel_data = of_device_get_match_data(dev);
	if (!_lf_panel_data)
		return -EINVAL;

	mipi_dsi_set_drvdata(dsi, lfp);
	lfp->dsi = dsi;
	lfp->data = _lf_panel_data;

#if USE_ORIENTATION
	pr_debug("lf_panel_probe: getting orientation\n");
	ret = of_drm_get_panel_orientation(dev->of_node, &lfp->orientation);
	if (ret) {
		dev_err_probe(dev, ret, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}
#endif

#if USE_POWER_REGULATOR
	lfp->power = devm_regulator_get(&dsi->dev, "power");
	if (IS_ERR(lfp->power))
		return dev_err_probe(dev, PTR_ERR(lfp->power),
				     "Couldn't get our power regulator\n");
#endif

	pr_debug("lf_panel_probe: getting reset GPIO\n");
	lfp->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lfp->reset)) {
		dev_err_probe(dev, PTR_ERR(lfp->reset), "Failed to get our reset GPIO\n");
		return PTR_ERR(lfp->reset);
	}

	pr_debug("lf_panel_probe: initializing DRM panel\n");
	lfp->base.prepare_prev_first = true;
	drm_panel_init(&lfp->base, dev, &lf_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	/* This appears last, as it's what will unblock the DSI host
	 * driver's component bind function.
	 */
	pr_debug("lf_panel_probe: adding DRM panel\n");
	drm_panel_add(&lfp->base);

	lfp->dsi->mode_flags = _lf_panel_data->mode_flags;
	lfp->dsi->format = MIPI_DSI_FMT_RGB888;
	lfp->dsi->lanes = _lf_panel_data->lanes;

	pr_debug("lf_panel_probe: attaching DSI\n");
	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);
		drm_panel_remove(&lfp->base);
		return ret;
	}

	return 0;
}

static void lf_panel_remove(struct mipi_dsi_device *dsi)
{
	pr_debug("lf_panel_remove\n");

	struct lf_panel *lfp = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	if (lfp->base.dev)
		drm_panel_remove(&lfp->base);
}

static const struct of_device_id lf_panel_of_ids[] = {
	{
		.compatible = "luckfox,lf101-8001280-ama",
		.data = &LF101_8001280_AMA_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, lf_panel_of_ids);

static struct mipi_dsi_driver lf_panel_driver = {
	.probe = lf_panel_probe,
	.remove = lf_panel_remove,
	.driver = {
		.name = LF_DSI_DRIVER_NAME,
		.of_match_table = lf_panel_of_ids,
	},
};
module_mipi_dsi_driver(lf_panel_driver);

MODULE_AUTHOR("Dimitar Lazarov <dimitar.lazarov@gmail.com>");
MODULE_DESCRIPTION("Luckfox DSI panel driver");
MODULE_LICENSE("GPL");
