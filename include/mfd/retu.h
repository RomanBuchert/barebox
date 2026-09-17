/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

struct retu_dev;

#define RETU_REG_WATCHDOG 0x17

int retu_read(struct retu_dev *rdev, unsigned int reg);
int retu_write(struct retu_dev *rdev, unsigned int reg, u16 value);
