/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <dma.h>
#include <linux/types.h>

void omap2_dispc_prepare_external_transfer(void);
void omap2_dispc_setup_plane(dma_addr_t framebuffer, unsigned int screen_width,
                             unsigned int x, unsigned int y,
                             unsigned int width, unsigned int height);
void omap2_dispc_enable_plane(bool enable);
void omap2_dispc_set_lcd_size(unsigned int width, unsigned int height);
void omap2_dispc_enable_lcd_out(bool enable);
void omap2_dispc_clear_irqstatus(void);
bool omap2_dispc_frame_done(void);
