/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/types.h>

#define OMAP2_RFBI_CS0 0

void omap2_rfbi_init(void);
void omap2_rfbi_select(unsigned int chip_select);
void omap2_rfbi_write_command(u16 value);
void omap2_rfbi_write_parameter(u16 value);
void omap2_rfbi_transfer(dma_addr_t framebuffer, unsigned int width,
                         unsigned int height);
