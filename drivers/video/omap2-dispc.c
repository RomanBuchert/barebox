// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP2420 DISPC support for the Nokia N800 Blizzard path.
 *
 * Hardware programming in this file is intentionally kept equivalent to
 * Nokia's RX-34 osso71 drivers/video/omap/dispc.c.  The initial RFBI routing
 * is the exact state-preserving sequence proven on real N800 hardware by
 * displaydiag V1.14.  We deliberately do not reset DISPC or rewrite FIFO,
 * load-mode, timing or divisor state inherited from NOLO.
 */

#include <common.h>
#include <linux/io.h>

#include "omap2-dispc.h"

#define OMAP2420_DISPC_BASE       0x48050400U

#define DISPC_IRQSTATUS            0x0018U
#define DISPC_CONTROL              0x0040U
#define DISPC_CONFIG               0x0044U
#define DISPC_DIVISOR              0x0070U
#define DISPC_SIZE_LCD             0x007cU
#define DISPC_GFX_BA0              0x0080U
#define DISPC_GFX_POSITION         0x0088U
#define DISPC_GFX_SIZE             0x008cU
#define DISPC_GFX_ATTRIBUTES       0x00a0U
#define DISPC_GFX_FIFO_THRESHOLD   0x00a4U
#define DISPC_GFX_FIFO_SIZE_STATUS 0x00a8U
#define DISPC_GFX_ROW_INC          0x00acU

#define DISPC_IRQ_FRAMEDONE        0x0001U
#define DISPC_RGB_16_BPP           0x06U
#define DISPC_BURST_8X32           1U

static inline u32 dispc_read_reg(u32 reg)
{
   return readl(IOMEM(OMAP2420_DISPC_BASE + reg));
}

static inline void dispc_write_reg(u32 reg, u32 value)
{
   writel(value, IOMEM(OMAP2420_DISPC_BASE + reg));
}

static void dispc_modify_reg(u32 reg, u32 mask, u32 value)
{
   u32 current = dispc_read_reg(reg);

   current &= ~mask;
   current |= value & mask;
   dispc_write_reg(reg, current);
}

static void omap2_dispc_prepare_rfbi_mode(void)
{
   u32 value;

   /*
    * Exact equivalent of Nokia osso71 enable_rfbi_mode(1), and exactly the
    * DISPC transition used by the hardware-verified displaydiag V1.14.
    */
   value = dispc_read_reg(DISPC_CONTROL);
   value &= ~((1U << 11) | (1U << 15) | (1U << 16));
   value |= (1U << 11) | (1U << 15);
   dispc_write_reg(DISPC_CONTROL, value);
}

void omap2_dispc_prepare_external_transfer(void)
{
   u32 size;
   u32 low;

   /*
    * These are the GFX-relevant operations performed by Nokia osso71
    * omap_dispc_init(..., ext_mode = 1) before the first Blizzard transfer.
    * The destructive DISPC soft reset is intentionally not repeated here:
    * displaydiag V1.14 proved the inherited NOLO DISPC state and RFBI route
    * on the real N800.  Everything below retains Nokia's register values.
    */
   dispc_modify_reg(DISPC_DIVISOR,
                    (0xffU << 16) | 0xffU, (1U << 16) | 2U);

   size = dispc_read_reg(DISPC_GFX_FIFO_SIZE_STATUS) & 0x1ffU;
   low = size * 3U / 4U;
   dispc_modify_reg(DISPC_GFX_FIFO_THRESHOLD,
                    (0x1ffU << 16) | 0x1ffU, (size << 16) | low);

   /* Nokia N800 MIPID panel: OMAP_LCDC_PANEL_TFT. */
   dispc_modify_reg(DISPC_CONTROL, 1U << 3, 1U << 3);

   /* DISPC_LOAD_FRAME_ONLY. */
   dispc_modify_reg(DISPC_CONFIG, 0x03U << 1, 0x02U << 1);

   /* set_lcd_data_lines(panel->bpp), panel bpp = 16. */
   dispc_modify_reg(DISPC_CONTROL, 0x03U << 8, 1U << 8);

   omap2_dispc_prepare_rfbi_mode();
}

void omap2_dispc_setup_plane(dma_addr_t framebuffer, unsigned int screen_width,
                             unsigned int x, unsigned int y,
                             unsigned int width, unsigned int height)
{
   dma_addr_t source = framebuffer + ((y * screen_width + x) * sizeof(u16));
   u32 value;

   /* Exact GFX/RGB565 subset of Nokia osso71 _setup_plane(). */
   value = dispc_read_reg(DISPC_GFX_ATTRIBUTES);
   value &= ~(0x0fU << 1);
   value |= DISPC_RGB_16_BPP << 1;
   value &= ~(1U << 9);              /* no color conversion */
   value &= ~(0x03U << 6);
   value |= DISPC_BURST_8X32 << 6;
   value &= ~(1U << 8);              /* OMAPFB_CHANNEL_OUT_LCD */
   dispc_write_reg(DISPC_GFX_ATTRIBUTES, value);

   dispc_write_reg(DISPC_GFX_BA0, source);
   dispc_modify_reg(DISPC_GFX_POSITION,
                    (0x7ffU << 16) | 0x7ffU, 0);
   dispc_modify_reg(DISPC_GFX_SIZE,
                    (0x7ffU << 16) | 0x7ffU,
                    ((height - 1U) << 16) | (width - 1U));
   dispc_write_reg(DISPC_GFX_ROW_INC,
                   (screen_width - width) * sizeof(u16) + 1U);
}

void omap2_dispc_enable_plane(bool enable)
{
   dispc_modify_reg(DISPC_GFX_ATTRIBUTES, 1U, enable ? 1U : 0U);
}

void omap2_dispc_set_lcd_size(unsigned int width, unsigned int height)
{
   dispc_modify_reg(DISPC_SIZE_LCD,
                    (0x7ffU << 16) | 0x7ffU,
                    ((height - 1U) << 16) | (width - 1U));
}

void omap2_dispc_enable_lcd_out(bool enable)
{
   dispc_modify_reg(DISPC_CONTROL, 1U, enable ? 1U : 0U);
}

void omap2_dispc_clear_irqstatus(void)
{
   u32 status = dispc_read_reg(DISPC_IRQSTATUS);

   dispc_write_reg(DISPC_IRQSTATUS, status);
}

bool omap2_dispc_frame_done(void)
{
   return !!(dispc_read_reg(DISPC_IRQSTATUS) & DISPC_IRQ_FRAMEDONE);
}
