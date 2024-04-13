/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * gp7101.h - Rohm gp7101 LEDs Driver
 */
#ifndef __GP7101_H__
#define __GP7101_H__

struct device;

struct gp7101_platform_data {
	struct device *fbdev;
	unsigned int def_value;
};

#endif