// SPDX-License-Identifier: GPL-2.0-only
/*
 * Epson S1D13744/S1D13745 controller operations used by the Nokia RX-34.
 *
 * The register sequences and calculations in this file are direct ports of
 * drivers/video/omap/blizzard.c from Nokia's patched RX-34 2.6.21 kernel.
 */

#include <clock.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <poller.h>

#include "n800-blizzard-core.h"

#define BLIZZARD_REV_CODE                    0x00U
#define BLIZZARD_CONFIG                      0x02U
#define BLIZZARD_PLL_DIV                     0x04U
#define BLIZZARD_PLL_CLOCK_SYNTH_0           0x08U
#define BLIZZARD_PLL_CLOCK_SYNTH_1           0x0aU
#define BLIZZARD_CLK_SRC                     0x0eU
#define BLIZZARD_HDISP                       0x2aU
#define BLIZZARD_HNDP                        0x2cU
#define BLIZZARD_VDISP0                      0x2eU
#define BLIZZARD_VDISP1                      0x30U
#define BLIZZARD_VNDP                        0x32U
#define BLIZZARD_HSW                         0x34U
#define BLIZZARD_VSW                         0x38U
#define BLIZZARD_INPUT_WIN_X_START_0         0x6cU
#define BLIZZARD_NDISP_CTRL_STATUS           0xe8U

#define BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE   0x01U
#define BLIZZARD_SRC_WRITE_LCD               0x00U
#define BLIZZARD_COLOR_RGB565                0x01U

#define BLIZZARD_VERSION_S1D13745            0x01U
#define BLIZZARD_VERSION_S1D13744            0x02U

static u8 blizzard_read_reg(struct n800_blizzard_bus *bus, u8 reg)
{
   bus->set_bits_per_cycle(bus->context, 8);
   return bus->read_reg(bus->context, reg);
}

static void blizzard_write_reg(struct n800_blizzard_bus *bus, u8 reg, u8 value)
{
   bus->set_bits_per_cycle(bus->context, 8);
   bus->write_command(bus->context, reg);
   bus->write_data8(bus->context, value);
}

int n800_blizzard_probe(struct n800_blizzard_bus *bus, unsigned long ext_hz,
                        struct n800_blizzard_info *info)
{
   unsigned int sys_div = 0;
   unsigned int sys_mul = 0;
   unsigned int pix_div;
   u8 pix_clk_src;
   u8 pll_div;

   /* blizzard_setup_clocks(): PLL must be locked before revision probing. */
   pll_div = blizzard_read_reg(bus, BLIZZARD_PLL_DIV);
   if (!(pll_div & 0x80U))
      return -ENODEV;

   /* calc_blizzard_clk_rates(). */
   pix_clk_src = blizzard_read_reg(bus, BLIZZARD_CLK_SRC);
   pix_div = ((pix_clk_src >> 3) & 0x1fU) + 1U;
   if ((pix_clk_src & (0x3U << 1)) == 0) {
      sys_div = (pll_div & 0x3fU) + 1U;
      sys_mul = blizzard_read_reg(bus, BLIZZARD_PLL_CLOCK_SYNTH_0);
      sys_mul |= (blizzard_read_reg(bus, BLIZZARD_PLL_CLOCK_SYNTH_1) & 0x0fU) << 11;
      if (!sys_mul)
         return -EINVAL;
      info->sys_hz = ext_hz * sys_mul / sys_div;
   } else {
      info->sys_hz = ext_hz;
   }
   info->pixel_hz = info->sys_hz / pix_div;

   info->revision = blizzard_read_reg(bus, BLIZZARD_REV_CODE);
   info->config = blizzard_read_reg(bus, BLIZZARD_CONFIG);
   switch (info->revision & 0xfcU) {
   case 0x9cU:
      break;
   case 0xa4U:
      break;
   default:
      return -ENODEV;
   }

   return 0;
}

int n800_blizzard_setup_tearsync(struct n800_blizzard_bus *bus,
                                 struct n800_blizzard_info *info,
                                 unsigned long write_cycle_ps)
{
   unsigned long pix_tx_time = write_cycle_ps;
   unsigned long line_upd_time;
   unsigned long min_tx_time;
   unsigned long max_tx_rate;
   unsigned int hdisp;
   unsigned int vdisp;
   unsigned int hndp;
   unsigned int vndp;
   unsigned int hsw;
   unsigned int vsw;
   unsigned int hs;
   unsigned int vs;
   bool hs_pol_inv;
   bool vs_pol_inv;
   bool use_hsvs;
   bool use_ndp;
   u8 value;

   /* Direct port of setup_tearsync() from Nokia blizzard.c. */
   hsw = blizzard_read_reg(bus, BLIZZARD_HSW);
   vsw = blizzard_read_reg(bus, BLIZZARD_VSW);
   hs_pol_inv = !(hsw & 0x80U);
   vs_pol_inv = !(vsw & 0x80U);
   hsw &= 0x7fU;
   vsw &= 0x3fU;

   hdisp = blizzard_read_reg(bus, BLIZZARD_HDISP) * 8U;
   vdisp = blizzard_read_reg(bus, BLIZZARD_VDISP0) +
           ((blizzard_read_reg(bus, BLIZZARD_VDISP1) & 0x03U) << 8);
   hndp = blizzard_read_reg(bus, BLIZZARD_HNDP) & 0x3fU;
   vndp = blizzard_read_reg(bus, BLIZZARD_VNDP);
   (void)vdisp;

   max_tx_rate = bus->get_max_tx_rate(bus->context);
   if (max_tx_rate) {
      min_tx_time = 1000000000UL / (max_tx_rate / 1000UL);
      if (pix_tx_time < min_tx_time)
         pix_tx_time = min_tx_time;
   }

   line_upd_time = (hdisp + hndp) * 1000000UL / (info->pixel_hz / 1000UL);
   line_upd_time *= 1000UL;
   use_hsvs = hdisp * pix_tx_time > line_upd_time;

   if (use_hsvs && (hs_pol_inv || vs_pol_inv)) {
      use_ndp = true;
      hs_pol_inv = false;
      vs_pol_inv = false;
      hs = hndp;
      vs = vndp;
   } else {
      use_ndp = false;
      hs = hsw;
      vs = vsw;
      if (!use_hsvs) {
         hs_pol_inv = false;
         vs_pol_inv = false;
      }
   }

   hs = hs * 1000000UL / (info->pixel_hz / 1000UL);
   hs *= 1000UL;
   vs = vs * (hdisp + hndp) * 1000000UL / (info->pixel_hz / 1000UL);
   vs *= 1000UL;

   if (vs <= hs)
      return -EDOM;
   vs = hs * 12U / 10U;
   if (hs > 10000U)
      hs = 10000U;

   value = blizzard_read_reg(bus, BLIZZARD_NDISP_CTRL_STATUS);
   value &= ~0x03U;
   value |= use_hsvs ? 1U : 0U;
   value |= (use_ndp && use_hsvs) ? 0U : 2U;
   blizzard_write_reg(bus, BLIZZARD_NDISP_CTRL_STATUS, value);

   info->vsync_only = !use_hsvs;

   return bus->setup_tearsync(bus->context, 1, hs, vs, hs_pol_inv, vs_pol_inv);
}

void n800_blizzard_wait_line_buffer(struct n800_blizzard_bus *bus)
{
   uint64_t start = get_time_ns();

   /* Direct port of blizzard_wait_line_buffer().  Timeout logs and continues. */
   while (blizzard_read_reg(bus, BLIZZARD_NDISP_CTRL_STATUS) & BIT(7)) {
      poller_call();
      if (is_timeout(start, 30ULL * MSECOND)) {
         pr_err("n800-blizzard: s1d1374x: line buffer not ready\n");
         break;
      }
   }
}

void n800_blizzard_disable_tearsync(struct n800_blizzard_bus *bus)
{
   u8 value;

   value = blizzard_read_reg(bus, BLIZZARD_NDISP_CTRL_STATUS);
   value &= ~BIT(3);
   blizzard_write_reg(bus, BLIZZARD_NDISP_CTRL_STATUS, value);
   (void)blizzard_read_reg(bus, BLIZZARD_NDISP_CTRL_STATUS);
}

void n800_blizzard_set_window(struct n800_blizzard_bus *bus,
                              const struct n800_blizzard_info *info,
                              u16 x, u16 y, u16 width, u16 height)
{
   u16 x_end = x + width - 1U;
   u16 y_end = y + height - 1U;
   u8 data[18];
   unsigned int i;

   /* Direct RGB565/no-zoom/no-overlay case of set_window_regs(). */
   data[0] = x;
   data[1] = x >> 8;
   data[2] = y;
   data[3] = y >> 8;
   data[4] = x_end;
   data[5] = x_end >> 8;
   data[6] = y_end;
   data[7] = y_end >> 8;
   data[8] = x;
   data[9] = x >> 8;
   data[10] = y;
   data[11] = y >> 8;
   data[12] = x_end;
   data[13] = x_end >> 8;
   data[14] = y_end;
   data[15] = y_end >> 8;
   data[16] = BLIZZARD_COLOR_RGB565;
   data[17] = (info->revision & 0xfcU) == 0x9cU ?
              BLIZZARD_SRC_WRITE_LCD : BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE;

   bus->set_bits_per_cycle(bus->context, 8);
   bus->write_command(bus->context, BLIZZARD_INPUT_WIN_X_START_0);
   for (i = 0; i < sizeof(data); i++)
      bus->write_data8(bus->context, data[i]);
}
