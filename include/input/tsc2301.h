/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/types.h>

#define TSC2301_PAGE_DATA     0U
#define TSC2301_PAGE_CONTROL  1U

#define TSC2301_REG_KPDATA    0x04U
#define TSC2301_REG_RESERVED0C 0x0cU
#define TSC2301_REG_KEY       0x01U
#define TSC2301_REG_RESET     0x04U
#define TSC2301_REG_CONFIG    0x05U
#define TSC2301_REG_CONFIG2   0x06U
#define TSC2301_REG_KPMASK    0x10U
#define TSC2301_REG_RESERVED07 0x07U

#define TSC2301_CONFIG_RESERVED_MASK  0xffc0U
#define TSC2301_CONFIG_RW_MASK        0x003fU

/* Keypad control values used by the Nokia N800 hardware sequence. */
#define TSC2301_KEY_STOP             0x4000U
#define TSC2301_KEY_DEBOUNCE_20MS    0x1000U
#define TSC2301_CONFIG2_KBC_SHIFT    14U
#define TSC2301_CONFIG2_KBC_MASK     (0x3U << TSC2301_CONFIG2_KBC_SHIFT)
#define TSC2301_CONFIG2_KBC_MODE2    (0x2U << TSC2301_CONFIG2_KBC_SHIFT)
#define TSC2301_N800_KEYPAD_MASK     0x8889U

struct tsc2301 {
   void *context;
   int (*transfer)(void *context, u16 command, u16 tx, u16 *rx);
};

u16 tsc2301_read_command(u8 page, u8 reg);
int tsc2301_read_reg(struct tsc2301 *tsc, u8 page, u8 reg, u16 *value);
int tsc2301_write_reg(struct tsc2301 *tsc, u8 page, u8 reg, u16 value);
