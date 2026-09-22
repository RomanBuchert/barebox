/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#define N800_BACKLIGHT_MAX 127U

int n800_backlight_get_brightness(unsigned int *brightness);
int n800_backlight_set_brightness(unsigned int brightness);
