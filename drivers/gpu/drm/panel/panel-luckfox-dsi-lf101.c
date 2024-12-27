// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2024 Dimitar Lazarov
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
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

struct lf_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;
	const struct drm_display_mode *mode;
	enum drm_panel_orientation orientation;
};

struct lf_panel_data {
	const struct drm_display_mode *mode;
	int lanes;
	unsigned long mode_flags;
};

/* 10.1inch 800x1280
 * https://www.luckfox.com/EN-LF101-8001280-AMA
 */
static const struct drm_display_mode LF101_8001280_AMA_mode = {
	.clock =        69907,
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
};

static const struct lf_panel_data LF101_8001280_AMA_data = {
	.mode = &LF101_8001280_AMA_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO,
};

static struct lf_panel *panel_to_ts(struct drm_panel *panel)
{
	return container_of(panel, struct lf_panel, base);
}

static int lf_panel_disable(struct drm_panel *panel)
{
	struct lf_panel *ts = panel_to_ts(panel);
	struct mipi_dsi_device *dsi = ts->dsi;

	int ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display off (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int lf_panel_unprepare(struct drm_panel *panel)
{
	struct lf_panel *ts = panel_to_ts(panel);
	struct mipi_dsi_device *dsi = ts->dsi;

	/* Enter sleep mode */
	int ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to enter sleep mode (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int lf_panel_prepare(struct drm_panel *panel)
{
	struct lf_panel *ts = panel_to_ts(panel);
	struct mipi_dsi_device *dsi = ts->dsi;

	/* Exit sleep mode and power on */
	int ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to exit sleep mode (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int lf_panel_enable(struct drm_panel *panel)
{
	struct lf_panel *ts = panel_to_ts(panel);
	struct mipi_dsi_device *dsi = ts->dsi;

	int ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to turn display on (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int lf_panel_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	struct lf_panel *ts = panel_to_ts(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ts->mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			ts->mode->hdisplay,
			ts->mode->vdisplay,
			drm_mode_vrefresh(ts->mode));
	}

	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	drm_connector_set_panel_orientation(connector, ts->orientation);

	return 1;
}

static enum drm_panel_orientation lf_panel_get_orientation(struct drm_panel *panel)
{
	struct lf_panel *ts = panel_to_ts(panel);

	return ts->orientation;
}

static const struct drm_panel_funcs lf_panel_funcs = {
	.disable = lf_panel_disable,
	.unprepare = lf_panel_unprepare,
	.prepare = lf_panel_prepare,
	.enable = lf_panel_enable,
	.get_modes = lf_panel_get_modes,
	.get_orientation = lf_panel_get_orientation,
};

static int lf_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lf_panel *ts;
	const struct lf_panel_data *_lf_panel_data;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	_lf_panel_data = of_device_get_match_data(dev);
	if (!_lf_panel_data)
		return -EINVAL;

	ts->mode = _lf_panel_data->mode;
	if (!ts->mode)
		return -EINVAL;

	mipi_dsi_set_drvdata(dsi, ts);
	ts->dsi = dsi;

	ret = of_drm_get_panel_orientation(dev->of_node, &ts->orientation);
	if (ret) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	drm_panel_init(&ts->base, dev, &lf_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	/* This appears last, as it's what will unblock the DSI host
	 * driver's component bind function.
	 */
	drm_panel_add(&ts->base);

	ts->dsi->mode_flags = _lf_panel_data->mode_flags;
	ts->dsi->format = MIPI_DSI_FMT_RGB888;
	ts->dsi->lanes = _lf_panel_data->lanes;

	ret = devm_mipi_dsi_attach(dev, ts->dsi);

	if (ret)
		dev_err(dev, "failed to attach dsi to host: %d\n", ret);

	return 0;
}

static void lf_panel_remove(struct mipi_dsi_device *dsi)
{
	struct lf_panel *ts = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ts->base);
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
