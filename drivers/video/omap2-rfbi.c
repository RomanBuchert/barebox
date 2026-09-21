// SPDX-License-Identifier: GPL-2.0-only

#include <clock.h>
#include <common.h>
#include <errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <poller.h>

#include "omap2-dispc.h"
#include "omap2-rfbi.h"

#define OMAP2420_RFBI_BASE         0x48050800U
#define OMAP2420_PRCM_BASE         0x48008000U
#define OMAP2420_CM_FCLKEN1_CORE   0x48008200U
#define OMAP2420_CM_ICLKEN1_CORE   0x48008210U
#define OMAP2420_CM_CLKSEL1_CORE   0x48008240U
#define OMAP2420_CM_CLKSEL1_PLL    0x48008540U
#define OMAP2420_CM_CLKSEL2_PLL    0x48008544U

#define PRCM_CLKSRC_CTRL            0x0060U

#define RFBI_SYSCONFIG              0x0010U
#define RFBI_SYSSTATUS              0x0014U
#define RFBI_CONTROL                0x0040U
#define RFBI_PIXEL_CNT              0x0044U
#define RFBI_LINE_NUMBER             0x0048U
#define RFBI_CMD                    0x004cU
#define RFBI_PARAM                  0x0050U
#define RFBI_READ                   0x0058U
#define RFBI_CONFIG0                0x0060U
#define RFBI_ONOFF_TIME0            0x0064U
#define RFBI_CYCLE_TIME0            0x0068U
#define RFBI_DATA_CYCLE1_0          0x006cU
#define RFBI_DATA_CYCLE2_0          0x0070U
#define RFBI_DATA_CYCLE3_0          0x0074U
#define RFBI_VSYNC_WIDTH             0x0090U
#define RFBI_HSYNC_WIDTH             0x0094U

#define RFBI_CONTROL_CS0            BIT(2)
#define RFBI_PARALLEL_MODE_MASK     0x3U
#define RFBI_PARALLEL_MODE_16       0x3U
#define RFBI_RESET_TIMEOUT          100U

struct rfbi_timings {
   int cs_on_time;
   int cs_off_time;
   int we_on_time;
   int we_off_time;
   int re_on_time;
   int re_off_time;
   int we_cycle_time;
   int re_cycle_time;
   int cs_pulse_width;
   int access_time;
   int clk_div;
   u32 onoff;
   u32 cycle;
   u32 divider;
};

static inline u32 rfbi_readl(u32 offset)
{
   return readl(IOMEM(OMAP2420_RFBI_BASE + offset));
}

static inline void rfbi_writel(u32 value, u32 offset)
{
   writel(value, IOMEM(OMAP2420_RFBI_BASE + offset));
}

static unsigned long round_to_tick(unsigned long ps, unsigned long period_ps, int div)
{
   unsigned long tick = period_ps * div;

   return (ps + tick - 1U) / tick * tick;
}

static int ps_to_ticks(int time_ps, unsigned long l4_khz, int div)
{
   unsigned long tick_ps = 1000000000UL / l4_khz * div;

   return (time_ps + tick_ps - 1U) / tick_ps;
}

static int convert_timings(struct rfbi_timings *t, unsigned long l4_khz)
{
   int reon, reoff, weon, weoff, cson, csoff;
   int cs_pulse, access, recyc, wecyc;
   u32 value;

   if (t->clk_div <= 0 || t->clk_div > 2)
      return -EINVAL;

   weon = ps_to_ticks(t->we_on_time, l4_khz, t->clk_div);
   weoff = ps_to_ticks(t->we_off_time, l4_khz, t->clk_div);
   if (weoff <= weon)
      weoff = weon + 1;
   reon = ps_to_ticks(t->re_on_time, l4_khz, t->clk_div);
   reoff = ps_to_ticks(t->re_off_time, l4_khz, t->clk_div);
   if (reoff <= reon)
      reoff = reon + 1;
   cson = ps_to_ticks(t->cs_on_time, l4_khz, t->clk_div);
   csoff = ps_to_ticks(t->cs_off_time, l4_khz, t->clk_div);
   if (csoff <= cson)
      csoff = cson + 1;
   csoff = max(csoff, max(weoff, reoff));

   if (weon > 0x0f || weoff > 0x3f || reon > 0x0f || reoff > 0x3f ||
       cson > 0x0f || csoff > 0x3f)
      return -ERANGE;

   value = cson | (csoff << 4) | (weon << 10) | (weoff << 14) |
           (reon << 20) | (reoff << 24);
   t->onoff = value;

   access = ps_to_ticks(t->access_time, l4_khz, t->clk_div);
   if (access <= reon)
      access = reon + 1;
   wecyc = max(ps_to_ticks(t->we_cycle_time, l4_khz, t->clk_div), weoff);
   recyc = max(ps_to_ticks(t->re_cycle_time, l4_khz, t->clk_div), reoff);
   cs_pulse = ps_to_ticks(t->cs_pulse_width, l4_khz, t->clk_div);
   if (access > 0x3f || wecyc > 0x3f || recyc > 0x3f || cs_pulse > 0x3f)
      return -ERANGE;

   t->cycle = wecyc | (recyc << 6) | (cs_pulse << 12) | (access << 22);
   t->divider = t->clk_div - 1;
   return 0;
}

static int calc_reg_timing(unsigned long sys_hz, unsigned long l4_khz, int div,
                           struct rfbi_timings *t)
{
   unsigned long period_ps = 1000000000UL / l4_khz;
   unsigned long systim = 1000000000UL / (sys_hz / 1000UL);

   memset(t, 0, sizeof(*t));
   t->clk_div = div;
   t->we_on_time = round_to_tick(2000, period_ps, div);
   t->re_on_time = round_to_tick(2000, period_ps, div);
   t->access_time = round_to_tick(t->re_on_time + 12200, period_ps, div);
   t->we_off_time = round_to_tick(t->we_on_time + 1000, period_ps, div);
   t->re_off_time = round_to_tick(t->re_on_time + 13000, period_ps, div);
   t->cs_off_time = round_to_tick(t->re_off_time + 1000, period_ps, div);
   t->we_cycle_time = max(round_to_tick(2 * systim + 2000, period_ps, div),
                          (unsigned long)t->we_off_time);
   t->re_cycle_time = max(round_to_tick(2 * systim + 2000, period_ps, div),
                          (unsigned long)t->re_off_time);

   return convert_timings(t, l4_khz);
}

static int read_clocks(struct omap2_rfbi_clocks *clocks)
{
   u32 clksrc = readl(IOMEM(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL));
   u32 pll1 = readl(IOMEM(OMAP2420_CM_CLKSEL1_PLL));
   u32 pll2 = readl(IOMEM(OMAP2420_CM_CLKSEL2_PLL));
   u32 core = readl(IOMEM(OMAP2420_CM_CLKSEL1_CORE));
   unsigned long crystal;
   unsigned long dpll;
   u32 crystal_sel = (pll1 >> 23) & 0x7U;
   u32 sys_div = (clksrc >> 6) & 0x3U;
   u32 mult = (pll1 >> 12) & 0x3ffU;
   u32 div = (pll1 >> 8) & 0x0fU;
   u32 amult = pll2 & 0x3U;
   u32 l3_div = core & 0x1fU;
   u32 l4_div = (core >> 5) & 0x3U;

   switch (crystal_sel) {
   case 0:
      crystal = 19200000UL;
      break;
   case 2:
      crystal = 13000000UL;
      break;
   case 3:
      crystal = 12000000UL;
      break;
   default:
      return -EINVAL;
   }

   if (!sys_div || !amult || !l3_div || !l4_div)
      return -EINVAL;

   clocks->osc_hz = crystal * sys_div;
   dpll = crystal * mult / (div + 1U);
   dpll *= amult;
   clocks->l4_hz = dpll / l3_div / l4_div;
   return 0;
}

int omap2_rfbi_init(struct omap2_rfbi_clocks *clocks)
{
   unsigned int timeout;
   u32 value;
   int ret;

   ret = read_clocks(clocks);
   if (ret)
      return ret;

   value = readl(IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   writel(value | 1U, IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   value = readl(IOMEM(OMAP2420_CM_FCLKEN1_CORE));
   writel(value | 1U, IOMEM(OMAP2420_CM_FCLKEN1_CORE));

   omap2_dispc_prepare_external_transfer();

   rfbi_writel(1U << 1, RFBI_SYSCONFIG);
   for (timeout = 0; timeout < RFBI_RESET_TIMEOUT; timeout++) {
      if (rfbi_readl(RFBI_SYSSTATUS) & 1U)
         break;
      mdelay(1);
   }
   if (timeout == RFBI_RESET_TIMEOUT)
      return -ETIMEDOUT;

   value = rfbi_readl(RFBI_SYSCONFIG);
   value |= (1U << 0) | (2U << 3);
   rfbi_writel(value, RFBI_SYSCONFIG);

   value = (0x03U << 0) | (0x01U << 5) | (0x02U << 7) |
           (1U << 20) | (1U << 21);
   rfbi_writel(value, RFBI_CONFIG0);
   rfbi_writel(0x10U, RFBI_DATA_CYCLE1_0);
   rfbi_writel(0, RFBI_DATA_CYCLE2_0);
   rfbi_writel(0, RFBI_DATA_CYCLE3_0);
   rfbi_writel(RFBI_CONTROL_CS0, RFBI_CONTROL);

   return 0;
}

int omap2_rfbi_set_timings(unsigned long device_sys_hz,
                           const struct omap2_rfbi_clocks *clocks,
                           unsigned long *write_cycle_ps)
{
   struct rfbi_timings timings;
   unsigned long l4_khz = clocks->l4_hz / 1000UL;
   u32 value;
   int div;
   int ret = -ERANGE;

   for (div = 1; div <= 2; div++) {
      ret = calc_reg_timing(device_sys_hz, l4_khz, div, &timings);
      if (!ret)
         break;
   }
   if (ret)
      return ret;

   rfbi_writel(timings.onoff, RFBI_ONOFF_TIME0);
   rfbi_writel(timings.cycle, RFBI_CYCLE_TIME0);
   value = rfbi_readl(RFBI_CONFIG0);
   value &= ~(1U << 4);
   value |= timings.divider << 4;
   rfbi_writel(value, RFBI_CONFIG0);
   if (write_cycle_ps)
      *write_cycle_ps = timings.we_cycle_time;
   return 0;
}

void omap2_rfbi_set_bits_per_cycle(unsigned int bits)
{
   u32 value = rfbi_readl(RFBI_CONFIG0);

   value &= ~RFBI_PARALLEL_MODE_MASK;
   if (bits == 16)
      value |= RFBI_PARALLEL_MODE_16;
   rfbi_writel(value, RFBI_CONFIG0);
}

u8 omap2_rfbi_read_reg8(u8 reg)
{
   omap2_rfbi_set_bits_per_cycle(8);
   rfbi_writel(reg, RFBI_CMD);
   rfbi_writel(0, RFBI_READ);
   return rfbi_readl(RFBI_READ) & 0xffU;
}

void omap2_rfbi_write_command8(u8 command)
{
   omap2_rfbi_set_bits_per_cycle(8);
   rfbi_writel(command, RFBI_CMD);
}

void omap2_rfbi_write_data8(u8 value)
{
   rfbi_writel(value, RFBI_PARAM);
}

int omap2_rfbi_setup_tearsync(unsigned int pin_count, unsigned int hs_pulse_ps,
                              unsigned int vs_pulse_ps, bool hs_pol_inv,
                              bool vs_pol_inv)
{
   unsigned long l4_khz;
   unsigned long tick_ps;
   unsigned int hs;
   unsigned int vs;
   unsigned int minimum;
   u32 value;

   if (pin_count != 1U && pin_count != 2U)
      return -EINVAL;

   /* Direct port of RX-34 rfbi_setup_tearsync(). */
   {
      struct omap2_rfbi_clocks clocks;

      if (read_clocks(&clocks))
         return -EINVAL;
      l4_khz = clocks.l4_hz / 1000UL;
   }
   tick_ps = 1000000000UL / l4_khz;
   hs = (hs_pulse_ps + tick_ps - 1U) / tick_ps;
   vs = (vs_pulse_ps + tick_ps - 1U) / tick_ps;
   if (hs < 2U)
      return -EDOM;
   minimum = pin_count == 2U ? 2U : 4U;
   if (vs < minimum)
      return -EDOM;
   if (vs == hs)
      return -EINVAL;

   rfbi_writel(hs, RFBI_HSYNC_WIDTH);
   rfbi_writel(vs, RFBI_VSYNC_WIDTH);

   value = rfbi_readl(RFBI_CONFIG0);
   if (hs_pol_inv)
      value &= ~(1U << 21);
   else
      value |= 1U << 21;
   if (vs_pol_inv)
      value &= ~(1U << 20);
   else
      value |= 1U << 20;

   /* The RX-34 source calculates this value but does not write CONFIG0 here. */
   (void)value;
   return 0;
}

unsigned long omap2_rfbi_get_max_tx_rate(void)
{
   struct omap2_rfbi_clocks clocks;

   if (read_clocks(&clocks))
      return 0;
   return clocks.l4_hz;
}

int omap2_rfbi_enable_tearsync(bool enable, unsigned int line)
{
   u32 value;

   if (line > ((1U << 11) - 1U))
      return -EINVAL;

   /* Nokia RX-34 rfbi_enable_tearsync(). */
   value = rfbi_readl(RFBI_CONFIG0);
   value &= ~(0x3U << 2);
   if (enable)
      value |= 1U << 2;
   rfbi_writel(value, RFBI_CONFIG0);
   rfbi_writel(line, RFBI_LINE_NUMBER);

   return 0;
}

int omap2_rfbi_transfer(unsigned int width, unsigned int height)
{
   uint64_t start;
   u32 value;

   if (!width || !height)
      return -EINVAL;

   /*
    * Equivalent to Nokia osso71 rfbi_transfer_area().  Linux completes this
    * asynchronously from the DISPC IRQ and rfbi_dma_callback().  Barebox has
    * no corresponding OMAP DSS IRQ infrastructure, so only completion is
    * adapted: poll FRAME_DONE while still servicing Barebox pollers.
    */
   omap2_dispc_set_lcd_size(width, height);
   rfbi_writel(width * height, RFBI_PIXEL_CNT);

   /* Polling replaces the Nokia DISPC interrupt callback. */
   omap2_dispc_clear_irqstatus();

   value = rfbi_readl(RFBI_CONTROL);
   value |= 1U;             /* enable */
   value |= 1U << 4;        /* internal trigger, reset by HW */
   rfbi_writel(value, RFBI_CONTROL);

   omap2_dispc_enable_lcd_out(true);

   start = get_time_ns();
   while (!omap2_dispc_frame_done()) {
      poller_call();
      if (is_timeout(start, 100ULL * MSECOND)) {
         value = rfbi_readl(RFBI_CONTROL);
         rfbi_writel(value & ~(1U << 0), RFBI_CONTROL);
         return -ETIMEDOUT;
      }
   }

   /*
    * A completed transfer can be fast enough that the polling loop above is
    * never entered.  Barebox pollers are cooperative, so service them once
    * unconditionally for every synchronous framebuffer transfer.  This does
    * not alter the RX-34 hardware sequence; it only gives infrastructure such
    * as the non-disableable Retu watchdog autoping poller a scheduling point
    * while fbconsole emits a long stream of damage updates.
    */
   poller_call();

   /* Equivalent to DISPC ISR acknowledgement followed by rfbi_dma_callback. */
   omap2_dispc_clear_irqstatus();
   value = rfbi_readl(RFBI_CONTROL);
   rfbi_writel(value & ~(1U << 0), RFBI_CONTROL);

   return 0;
}
