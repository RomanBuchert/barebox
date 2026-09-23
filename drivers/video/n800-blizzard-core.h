/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/types.h>

#define N800_BLIZZARD_WIDTH  800U
#define N800_BLIZZARD_HEIGHT 480U

struct n800_blizzard_bus {
   void *context;
   void (*set_bits_per_cycle)(void *context, unsigned int bits);
   u8 (*read_reg)(void *context, u8 reg);
   void (*write_command)(void *context, u8 command);
   void (*write_data8)(void *context, u8 value);
   int (*setup_tearsync)(void *context, unsigned int pin_count,
                         unsigned int hs_pulse_ps, unsigned int vs_pulse_ps,
                         bool hs_pol_inv, bool vs_pol_inv);
   unsigned long (*get_max_tx_rate)(void *context);
};

struct n800_blizzard_info {
   u8 revision;
   u8 config;
   unsigned long sys_hz;
   unsigned long pixel_hz;
   bool vsync_only;
};

int n800_blizzard_probe(struct n800_blizzard_bus *bus, unsigned long ext_hz,
                        struct n800_blizzard_info *info);
int n800_blizzard_setup_tearsync(struct n800_blizzard_bus *bus,
                                 struct n800_blizzard_info *info,
                                 unsigned long write_cycle_ps);
void n800_blizzard_wait_line_buffer(struct n800_blizzard_bus *bus);
void n800_blizzard_disable_tearsync(struct n800_blizzard_bus *bus);
void n800_blizzard_set_window(struct n800_blizzard_bus *bus,
                              const struct n800_blizzard_info *info,
                              u16 x, u16 y, u16 width, u16 height);

/*
 * RX-34 runtime power management. Initial S1D13745 cold initialization remains
 * a NOLO responsibility; these functions only suspend/resume a valid state.
 */
int n800_blizzard_suspend(struct n800_blizzard_bus *bus);
int n800_blizzard_resume(struct n800_blizzard_bus *bus);
