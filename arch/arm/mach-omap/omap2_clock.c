// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <linux/io.h>
#include <mach/omap/omap2-clock.h>

#define OMAP2420_PRCM_BASE         0x48008000U
#define OMAP2420_CM_FCLKEN1_CORE   0x48008200U
#define OMAP2420_CM_ICLKEN1_CORE   0x48008210U
#define OMAP2420_CM_CLKSEL1_CORE   0x48008240U
#define OMAP2420_CM_CLKSEL1_PLL    0x48008540U
#define OMAP2420_CM_CLKSEL2_PLL    0x48008544U

#define PRCM_CLKSRC_CTRL           0x0060U

static int omap2_read_clock_rates(unsigned long *osc_hz, unsigned long *l4_hz)
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

   *osc_hz = crystal * sys_div;
   dpll = crystal * mult / (div + 1U);
   dpll *= amult;
   *l4_hz = dpll / l3_div / l4_div;

   return 0;
}

unsigned long omap2_get_osc_clock_rate(void)
{
   unsigned long osc_hz;
   unsigned long l4_hz;

   if (omap2_read_clock_rates(&osc_hz, &l4_hz))
      return 0;

   return osc_hz;
}

unsigned long omap2_get_l4_clock_rate(void)
{
   unsigned long osc_hz;
   unsigned long l4_hz;

   if (omap2_read_clock_rates(&osc_hz, &l4_hz))
      return 0;

   return l4_hz;
}

void omap2_enable_dss_clocks(void)
{
   u32 value;

   value = readl(IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   writel(value | 1U, IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   value = readl(IOMEM(OMAP2420_CM_FCLKEN1_CORE));
   writel(value | 1U, IOMEM(OMAP2420_CM_FCLKEN1_CORE));
}

void omap2_enable_oscillator(void)
{
   u32 value;

   value = readl(IOMEM(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL));
   writel(value & ~(0x3U << 3), IOMEM(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL));
}
