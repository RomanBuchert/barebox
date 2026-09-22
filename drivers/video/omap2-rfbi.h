/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <dma.h>
#include <linux/types.h>

int omap2_rfbi_init(void);
unsigned long omap2_rfbi_get_osc_rate(void);
int omap2_rfbi_set_timings(unsigned long device_sys_hz,
                           unsigned long *write_cycle_ps);
void omap2_rfbi_set_bits_per_cycle(unsigned int bits);
u8 omap2_rfbi_read_reg8(u8 reg);
void omap2_rfbi_write_command8(u8 command);
void omap2_rfbi_write_data8(u8 value);
int omap2_rfbi_setup_tearsync(unsigned int pin_count, unsigned int hs_pulse_ps,
                              unsigned int vs_pulse_ps, bool hs_pol_inv,
                              bool vs_pol_inv);
unsigned long omap2_rfbi_get_max_tx_rate(void);
int omap2_rfbi_enable_tearsync(bool enable, unsigned int line);
int omap2_rfbi_transfer(unsigned int width, unsigned int height);
