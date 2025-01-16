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
#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#define LF_DSI_DRIVER_NAME "panel-lf-dsi-lf101"

#define USE_POWER_REGULATOR 0
#define USE_ORIENTATION 0
#define PANEL_DCS_IN_PREPARE 1
#define READ_PANEL_ID 0

#define JD9365_CMD_PAGE (0xE0)
#define JD9365_PAGE_USER (0x00)

#define JD9365_CMD_DSI_INIT0 (0x80)
#define JD9365_DSI_1_LANE (0x00)
#define JD9365_DSI_2_LANE (0x01)
#define JD9365_DSI_3_LANE (0x02)
#define JD9365_DSI_4_LANE (0x03)

#define JD9365_CMD_GS_BIT (1 << 0)
#define JD9365_CMD_SS_BIT (1 << 1)

#define MAX_CMD_DATA_LEN		1
#define REGFLAG_DELAY			0xFF

struct LCM_init_cmd {
	u8 cmd;
	u8 data_bytes;
	u8 data[MAX_CMD_DATA_LEN];
};

struct lf_panel_data {
	const struct drm_display_mode *mode;
	int lanes;
	unsigned long mode_flags;
    uint8_t madctl_val;
    uint8_t colmod_val;
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
static const struct drm_display_mode LF101_8001280_AMA_4LANE_mode = {
	.clock =        70000, // 69907,
	.hdisplay =     800,                    // hactive
	.hsync_start =  800 + 40,               // hactive + hfp
	.hsync_end =    800 + 40 + 20,          // hactive + hfp + hsync
	.htotal =       800 + 40 + 20 + 20,     // hactive + hfp + hsync + hbp
	.vdisplay =     1280,                   // vactive
	.vsync_start =  1280 + 20,              // vactive + vfp
	.vsync_end =    1280 + 20 + 4,          // vactive + vfp + vsync
	.vtotal =       1280 + 20 + 4 + 20,     // vactive + vfp + vsync + vbp
    .width_mm =     135,
    .height_mm =    216,
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct LCM_init_cmd LF101_8001280_AMA_4LANE_init_cmds[] = {
	{REGFLAG_DELAY,10,{}},
	// Page 0
	{0xE0,1,{0x00}},
	// Password
	{0xE1,1,{0x93}},
	{0xE2,1,{0x65}},
	{0xE3,1,{0xF8}},
	// Sequence Ctrl
	{0x80,1,{0x03}}, //0x03:4-Lane；0x02:3-Lane；0x01:2-Lane；0x00:1-Lane

	// Page 1
	{0xE0,1,{0x01}},
	// VCOM
	{0x00,1,{0x00}},
	{0x01,1,{0x3B}},
	//
	{0x0C,1,{0x74}},
	// Set Gamma Power, VGMP,VGMN,VGSP,VGSN
	{0x17,1,{0x00}},
	{0x18,1,{0xAF}},
	{0x19,1,{0x00}},
	{0x1A,1,{0x00}},
	{0x1B,1,{0xAF}},
	{0x1C,1,{0x00}},
	// 
	{0x35,1,{0x26}},
	// SETPANEL
	{0x37,1,{0x09}},
	// SET RGBCYC
	{0x38,1,{0x04}},
	{0x39,1,{0x00}},
	{0x3A,1,{0x01}},
	{0x3C,1,{0x78}},
	{0x3D,1,{0xFF}},
	{0x3E,1,{0xFF}},
	{0x3F,1,{0x7F}},
	// TCON
	{0x40,1,{0x06}},
	{0x41,1,{0xA0}},
	//
	{0x42,1,{0x81}},
	{0x43,1,{0x14}},
	{0x44,1,{0x23}},
	{0x45,1,{0x28}},
	// Power voltage
	{0x55,1,{0x02}},
	{0x57,1,{0x69}},
	{0x59,1,{0x0A}},
	{0x5A,1,{0x2A}},
	{0x5B,1,{0x17}},
	// Gamma
	{0x5D,1,{0x7F}},
	{0x5E,1,{0x6B}},
	{0x5F,1,{0x5C}},
	{0x60,1,{0x4F}},
	{0x61,1,{0x4D}},
	{0x62,1,{0x3F}},
	{0x63,1,{0x42}},
	{0x64,1,{0x2B}},
	{0x65,1,{0x44}},
	{0x66,1,{0x43}},
	{0x67,1,{0x43}},
	{0x68,1,{0x63}},
	{0x69,1,{0x52}},
	{0x6A,1,{0x5A}},
	{0x6B,1,{0x4F}},
	{0x6C,1,{0x4E}},
	{0x6D,1,{0x20}},
	{0x6E,1,{0x0F}},
	{0x6F,1,{0x00}},
	{0x70,1,{0x7F}},
	{0x71,1,{0x6B}},
	{0x72,1,{0x5C}},
	{0x73,1,{0x4F}},
	{0x74,1,{0x4D}},
	{0x75,1,{0x3F}},
	{0x76,1,{0x42}},
	{0x77,1,{0x2B}},
	{0x78,1,{0x44}},
	{0x79,1,{0x43}},
	{0x7A,1,{0x43}},
	{0x7B,1,{0x63}},
	{0x7C,1,{0x52}},
	{0x7D,1,{0x5A}},
	{0x7E,1,{0x4F}},
	{0x7F,1,{0x4E}},
	{0x80,1,{0x20}},
	{0x81,1,{0x0F}},
	{0x82,1,{0x00}},

	// Page 2
	{0xE0,1,{0x02}},
	// GIP_L
	{0x00,1,{0x02}},
	{0x01,1,{0x02}},
	{0x02,1,{0x00}},
	{0x03,1,{0x00}},
	{0x04,1,{0x1E}},
	{0x05,1,{0x1E}},
	{0x06,1,{0x1F}},
	{0x07,1,{0x1F}},
	{0x08,1,{0x1F}},
	{0x09,1,{0x17}},
	{0x0A,1,{0x17}},
	{0x0B,1,{0x37}},
	{0x0C,1,{0x37}},
	{0x0D,1,{0x47}},
	{0x0E,1,{0x47}},
	{0x0F,1,{0x45}},
	{0x10,1,{0x45}},
	{0x11,1,{0x4B}},
	{0x12,1,{0x4B}},
	{0x13,1,{0x49}},
	{0x14,1,{0x49}},
	{0x15,1,{0x1F}},
	// GIP_R
	{0x16,1,{0x01}},
	{0x17,1,{0x01}},
	{0x18,1,{0x00}},
	{0x19,1,{0x00}},
	{0x1A,1,{0x1E}},
	{0x1B,1,{0x1E}},
	{0x1C,1,{0x1F}},
	{0x1D,1,{0x1F}},
	{0x1E,1,{0x1F}},
	{0x1F,1,{0x17}},
	{0x20,1,{0x17}},
	{0x21,1,{0x37}},
	{0x22,1,{0x37}},
	{0x23,1,{0x46}},
	{0x24,1,{0x46}},
	{0x25,1,{0x44}},
	{0x26,1,{0x44}},
	{0x27,1,{0x4A}},
	{0x28,1,{0x4A}},
	{0x29,1,{0x48}},
	{0x2A,1,{0x48}},
	{0x2B,1,{0x1F}},
	// GIP_L_GS
	{0x2C,1,{0x01}},
	{0x2D,1,{0x01}},
	{0x2E,1,{0x00}},
	{0x2F,1,{0x00}},
	{0x30,1,{0x1F}},
	{0x31,1,{0x1F}},
	{0x32,1,{0x1E}},
	{0x33,1,{0x1E}},
	{0x34,1,{0x1F}},
	{0x35,1,{0x17}},
	{0x36,1,{0x17}},
	{0x37,1,{0x37}},
	{0x38,1,{0x37}},
	{0x39,1,{0x08}},
	{0x3A,1,{0x08}},
	{0x3B,1,{0x0A}},
	{0x3C,1,{0x0A}},
	{0x3D,1,{0x04}},
	{0x3E,1,{0x04}},
	{0x3F,1,{0x06}},
	{0x40,1,{0x06}},
	{0x41,1,{0x1F}},
	// GIP_R_GS
	{0x42,1,{0x02}},
	{0x43,1,{0x02}},
	{0x44,1,{0x00}},
	{0x45,1,{0x00}},
	{0x46,1,{0x1F}},
	{0x47,1,{0x1F}},
	{0x48,1,{0x1E}},
	{0x49,1,{0x1E}},
	{0x4A,1,{0x1F}},
	{0x4B,1,{0x17}},
	{0x4C,1,{0x17}},
	{0x4D,1,{0x37}},
	{0x4E,1,{0x37}},
	{0x4F,1,{0x09}},
	{0x50,1,{0x09}},
	{0x51,1,{0x0B}},
	{0x52,1,{0x0B}},
	{0x53,1,{0x05}},
	{0x54,1,{0x05}},
	{0x55,1,{0x07}},
	{0x56,1,{0x07}},
	{0x57,1,{0x1F}},
	// GIP timing
	{0x58,1,{0x40}},
	{0x5B,1,{0x30}},
	{0x5C,1,{0x16}},
	{0x5D,1,{0x34}},
	{0x5E,1,{0x05}},
	{0x5F,1,{0x02}},
	{0x63,1,{0x00}},
	{0x64,1,{0x6A}},
	{0x67,1,{0x73}},
	{0x68,1,{0x1D}},
	{0x69,1,{0x08}},
	{0x6A,1,{0x6A}},
	{0x6B,1,{0x08}},
	{0x6C,1,{0x00}},
	{0x6D,1,{0x00}},
	{0x6E,1,{0x00}},
	{0x6F,1,{0x88}},
	{0x75,1,{0xFF}},
	{0x77,1,{0xDD}},
	{0x78,1,{0x3F}},
	{0x79,1,{0x15}},
	{0x7A,1,{0x17}},
	{0x7D,1,{0x14}},
	{0x7E,1,{0x82}},
	
	// Page 4
	{0xE0,1,{0x04}},

	{0x00,1,{0x0E}},
	{0x02,1,{0xB3}},
	{0x09,1,{0x61}},
	{0x0E,1,{0x48}},

	// Page 0
	{0xE0,1,{0x00}},
	{0xE6,1,{0x02}},
	{0xE7,1,{0x0C}},
	// SLPOUT
	{0x11,1,{0x00}},
	{REGFLAG_DELAY,120,{}},
};

static const struct lf_panel_data LF101_8001280_AMA_4LANE_data = {
	.mode = &LF101_8001280_AMA_4LANE_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_NO_EOT_PACKET, // | MIPI_DSI_MODE_LPM,
    .madctl_val = 0x00, // RGB
    .colmod_val = 0x77, // RGB888
	.init_cmds = LF101_8001280_AMA_4LANE_init_cmds,
	.init_cmds_count = ARRAY_SIZE(LF101_8001280_AMA_4LANE_init_cmds),
};

static const struct drm_display_mode LF101_8001280_AMA_2LANE_mode = {
	.clock =        70000, // 69907,
	.hdisplay =     800,                    // hactive
	.hsync_start =  800 + 40,               // hactive + hfp
	.hsync_end =    800 + 40 + 20,          // hactive + hfp + hsync
	.htotal =       800 + 40 + 20 + 20,     // hactive + hfp + hsync + hbp
	.vdisplay =     1280,                   // vactive
	.vsync_start =  1280 + 30,              // vactive + vfp
	.vsync_end =    1280 + 30 + 4,          // vactive + vfp + vsync
	.vtotal =       1280 + 30 + 4 + 10,     // vactive + vfp + vsync + vbp
    .width_mm =     135,
    .height_mm =    216,
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct LCM_init_cmd LF101_8001280_AMA_2LANE_init_cmds[] = {
	{0xE0,1,{0x00}},
	{0xE1,1,{0x93}},
	{0xE2,1,{0x65}},
	{0xE3,1,{0xF8}},
	{0x80,1,{0x01}},
	{0xE0,1,{0x01}},
	{0x00,1,{0x00}},
	{0x01,1,{0x38}},
	{0x03,1,{0x10}},
	{0x04,1,{0x38}},
	{0x0C,1,{0x74}},
	{0x17,1,{0x00}},
	{0x18,1,{0xAF}},
	{0x19,1,{0x00}},
	{0x1A,1,{0x00}},
	{0x1B,1,{0xAF}},
	{0x1C,1,{0x00}},
	{0x35,1,{0x26}},
	{0x37,1,{0x09}},
	{0x38,1,{0x04}},
	{0x39,1,{0x00}},
	{0x3A,1,{0x01}},
	{0x3C,1,{0x78}},
	{0x3D,1,{0xFF}},
	{0x3E,1,{0xFF}},
	{0x3F,1,{0x7F}},
	{0x40,1,{0x06}},
	{0x41,1,{0xA0}},
	{0x42,1,{0x81}},
	{0x43,1,{0x1E}},
	{0x44,1,{0x0D}},
	{0x45,1,{0x28}},
	//{0x4A,1,{0x35}},//bist
	{0x55,1,{0x02}},
	{0x57,1,{0x69}},
	{0x59,1,{0x0A}},
	{0x5A,1,{0x2A}},
	{0x5B,1,{0x17}},
	{0x5D,1,{0x7F}},
	{0x5E,1,{0x6A}},
	{0x5F,1,{0x5B}},
	{0x60,1,{0x4F}},
	{0x61,1,{0x4A}},
	{0x62,1,{0x3D}},
	{0x63,1,{0x41}},
	{0x64,1,{0x2A}},
	{0x65,1,{0x44}},
	{0x66,1,{0x43}},
	{0x67,1,{0x44}},
	{0x68,1,{0x62}},
	{0x69,1,{0x52}},
	{0x6A,1,{0x59}},
	{0x6B,1,{0x4C}},
	{0x6C,1,{0x48}},
	{0x6D,1,{0x3A}},
	{0x6E,1,{0x26}},
	{0x6F,1,{0x00}},
	{0x70,1,{0x7F}},
	{0x71,1,{0x6A}},
	{0x72,1,{0x5B}},
	{0x73,1,{0x4F}},
	{0x74,1,{0x4A}},
	{0x75,1,{0x3D}},
	{0x76,1,{0x41}},
	{0x77,1,{0x2A}},
	{0x78,1,{0x44}},
	{0x79,1,{0x43}},
	{0x7A,1,{0x44}},
	{0x7B,1,{0x62}},
	{0x7C,1,{0x52}},
	{0x7D,1,{0x59}},
	{0x7E,1,{0x4C}},
	{0x7F,1,{0x48}},
	{0x80,1,{0x3A}},
	{0x81,1,{0x26}},
	{0x82,1,{0x00}},
	{0xE0,1,{0x02}},
	{0x00,1,{0x42}},
	{0x01,1,{0x42}},
	{0x02,1,{0x40}},
	{0x03,1,{0x40}},
	{0x04,1,{0x5E}},
	{0x05,1,{0x5E}},
	{0x06,1,{0x5F}},
	{0x07,1,{0x5F}},
	{0x08,1,{0x5F}},
	{0x09,1,{0x57}},
	{0x0A,1,{0x57}},
	{0x0B,1,{0x77}},
	{0x0C,1,{0x77}},
	{0x0D,1,{0x47}},
	{0x0E,1,{0x47}},
	{0x0F,1,{0x45}},
	{0x10,1,{0x45}},
	{0x11,1,{0x4B}},
	{0x12,1,{0x4B}},
	{0x13,1,{0x49}},
	{0x14,1,{0x49}},
	{0x15,1,{0x5F}},
	{0x16,1,{0x41}},
	{0x17,1,{0x41}},
	{0x18,1,{0x40}},
	{0x19,1,{0x40}},
	{0x1A,1,{0x5E}},
	{0x1B,1,{0x5E}},
	{0x1C,1,{0x5F}},
	{0x1D,1,{0x5F}},
	{0x1E,1,{0x5F}},
	{0x1F,1,{0x57}},
	{0x20,1,{0x57}},
	{0x21,1,{0x77}},
	{0x22,1,{0x77}},
	{0x23,1,{0x46}},
	{0x24,1,{0x46}},
	{0x25,1,{0x44}},
	{0x26,1,{0x44}},
	{0x27,1,{0x4A}},
	{0x28,1,{0x4A}},
	{0x29,1,{0x48}},
	{0x2A,1,{0x48}},
	{0x2B,1,{0x5F}},
	{0x2C,1,{0x01}},
	{0x2D,1,{0x01}},
	{0x2E,1,{0x00}},
	{0x2F,1,{0x00}},
	{0x30,1,{0x1F}},
	{0x31,1,{0x1F}},
	{0x32,1,{0x1E}},
	{0x33,1,{0x1E}},
	{0x34,1,{0x1F}},
	{0x35,1,{0x17}},
	{0x36,1,{0x17}},
	{0x37,1,{0x37}},
	{0x38,1,{0x37}},
	{0x39,1,{0x08}},
	{0x3A,1,{0x08}},
	{0x3B,1,{0x0A}},
	{0x3C,1,{0x0A}},
	{0x3D,1,{0x04}},
	{0x3E,1,{0x04}},
	{0x3F,1,{0x06}},
	{0x40,1,{0x06}},
	{0x41,1,{0x1F}},
	{0x42,1,{0x02}},
	{0x43,1,{0x02}},
	{0x44,1,{0x00}},
	{0x45,1,{0x00}},
	{0x46,1,{0x1F}},
	{0x47,1,{0x1F}},
	{0x48,1,{0x1E}},
	{0x49,1,{0x1E}},
	{0x4A,1,{0x1F}},
	{0x4B,1,{0x17}},
	{0x4C,1,{0x17}},
	{0x4D,1,{0x37}},
	{0x4E,1,{0x37}},
	{0x4F,1,{0x09}},
	{0x50,1,{0x09}},
	{0x51,1,{0x0B}},
	{0x52,1,{0x0B}},
	{0x53,1,{0x05}},
	{0x54,1,{0x05}},
	{0x55,1,{0x07}},
	{0x56,1,{0x07}},
	{0x57,1,{0x1F}},
	{0x58,1,{0x40}},
	{0x5B,1,{0x30}},
	{0x5C,1,{0x00}},
	{0x5D,1,{0x34}},
	{0x5E,1,{0x05}},
	{0x5F,1,{0x02}},
	{0x63,1,{0x00}},
	{0x64,1,{0x6A}},
	{0x67,1,{0x73}},
	{0x68,1,{0x07}},
	{0x69,1,{0x08}},
	{0x6A,1,{0x6A}},
	{0x6B,1,{0x08}},
	{0x6C,1,{0x00}},
	{0x6D,1,{0x00}},
	{0x6E,1,{0x00}},
	{0x6F,1,{0x88}},
	{0x75,1,{0xFF}},
	{0x77,1,{0xDD}},
	{0x78,1,{0x2C}},
	{0x79,1,{0x15}},
	{0x7A,1,{0x17}},
	{0x7D,1,{0x14}},
	{0x7E,1,{0x82}},
	{0xE0,1,{0x04}},
	{0x00,1,{0x0E}},
	{0x02,1,{0xB3}},
	{0x09,1,{0x61}},
	{0x0E,1,{0x48}},
	{0x37,1,{0x58}},
	{0x2B,1,{0x0F}},
	{0xE0,1,{0x00}},
	{0xE6,1,{0x02}},
	{0xE7,1,{0x0C}},
	{0x11,1,{0x00}},
	{REGFLAG_DELAY,120,{}},
	{0xE0,1,{0x00}},
	{0x29,1,{0x00}},
	{REGFLAG_DELAY,5,{}},	
};

static const struct lf_panel_data LF101_8001280_AMA_2LANE_data = {
	.mode = &LF101_8001280_AMA_2LANE_mode,
	.lanes = 2,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_NO_EOT_PACKET, // | MIPI_DSI_MODE_LPM,
    .madctl_val = 0x00, // RGB
    .colmod_val = 0x77, // RGB888
	.init_cmds = LF101_8001280_AMA_2LANE_init_cmds,
	.init_cmds_count = ARRAY_SIZE(LF101_8001280_AMA_2LANE_init_cmds),
};

static int lf_panel_set_init_cmds(struct mipi_dsi_device *dsi, const struct lf_panel_data *data)
{
	pr_debug("lf_panel_set_init_cmds\n");

	int i;
	int ret;

    uint8_t lane_command = JD9365_DSI_2_LANE;
    bool is_user_set = true;
    bool is_cmd_overwritten = false;

    switch (data->lanes)
    {
    case 1:
        lane_command = JD9365_DSI_1_LANE;
        break;
    case 2:
        lane_command = JD9365_DSI_2_LANE;
        break;
    case 3:
        lane_command = JD9365_DSI_3_LANE;
        break;
    case 4:
        lane_command = JD9365_DSI_4_LANE;
        break;
    default:
        return -1;
    }

#if READ_PANEL_ID
    uint8_t ID[3];
	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_GET_DISPLAY_ID, ID, 3);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to read ID: %d\n", ret);
		return ret;
	}
	pr_debug("lf_panel_set_init_cmds: LCD ID %02X %02X %02X", ID[0], ID[1], ID[2]);
#endif

	u8 page = JD9365_PAGE_USER;
	ret = mipi_dsi_dcs_write(dsi, JD9365_CMD_PAGE, &page, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to write page 0x%02X: %d\n", page, ret);
		return ret;
	}
	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_ADDRESS_MODE, &data->madctl_val, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to write MADCTL 0x%02X: %d\n", data->madctl_val, ret);
		return ret;
	}
	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_PIXEL_FORMAT, &data->colmod_val, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to write COLMOD 0x%02X: %d\n", data->colmod_val, ret);
		return ret;
	}
	ret = mipi_dsi_dcs_write(dsi, JD9365_CMD_DSI_INIT0, &lane_command, 1);
	if (ret < 0) {
		dev_err(&dsi->dev, "failed to write DSI_INT0 0x%02X: %d\n", lane_command, ret);
		return ret;
	}

	for (i = 0; i < data->init_cmds_count; i++) {
		const struct LCM_init_cmd *init_cmd = &data->init_cmds[i];

		if (init_cmd->cmd == REGFLAG_DELAY) {
			msleep(init_cmd->data_bytes);
			continue;
		}

		if (init_cmd->data_bytes == 0) {
			continue;
		}

		pr_debug("lf_panel_set_init_cmds: cmd: 0x%02X, data: 0x%02X\n", init_cmd->cmd, init_cmd->data[0]);

	    // Check if the command has been used or conflicts with the internal
        if (is_user_set)
        {
            switch (init_cmd->cmd)
            {
            case MIPI_DCS_SET_ADDRESS_MODE:
				if (init_cmd->data[0] != data->madctl_val) {
                	is_cmd_overwritten = true;
                	//data->madctl_val = cmd->data[0];
				}
                break;
            case MIPI_DCS_SET_PIXEL_FORMAT:
				if (init_cmd->data[0] != data->colmod_val) {
					is_cmd_overwritten = true;
					//data->colmod_val = cmd->data[0];
				}
                break;
			case JD9365_CMD_DSI_INIT0:
				if (init_cmd->data[0] != lane_command)
				{
					is_cmd_overwritten = true;
					//data->lanes = init_cmd->data[0];
				}
				break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten)
            {
                is_cmd_overwritten = false;
                pr_debug("lf_panel_set_init_cmds: The 0x%02X command has been used and will be overwritten by external initialization sequence\n", init_cmd->cmd);
            }
        }

		ret = mipi_dsi_dcs_write(dsi, init_cmd->cmd, init_cmd->data, init_cmd->data_bytes);
		if (ret < 0) {
			dev_err(&dsi->dev, "failed to write command: %d\n", ret);
			return ret;
		}

        // Check if the current cmd is the "page set" cmd
        if (init_cmd->cmd == JD9365_CMD_PAGE)
        {
            is_user_set = (init_cmd->data[0] == JD9365_PAGE_USER);
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
	ret = lf_panel_set_init_cmds(dsi, lfp->data);
	if (ret) {
		dev_err(&dsi->dev, "failed to set init cmds (%d)\n", ret);
		goto poweroff;
	}

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
	ret = lf_panel_set_init_cmds(dsi, lfp->data->init_cmds, lfp->data->init_cmds_count);
	if (ret) {
		dev_err(&dsi->dev, "failed to set init cmds (%d)\n", ret);
		return ret;
	}

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
		.data = &LF101_8001280_AMA_4LANE_data,
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
