/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

int cbus_gpio_read_reg(unsigned int dev, unsigned int reg);
int cbus_gpio_write_reg(unsigned int dev, unsigned int reg, unsigned int value);
