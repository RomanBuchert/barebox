// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <errno.h>
#include <io.h>
#include <i2c/i2c-cbus-gpio.h>
#include <linux/bitops.h>
#include <mach/omap/omap2-clock.h>

#include "n800-display-power.h"

#define N800_GPIO1_BASE             0x48018000U
#define N800_GPIO_OE_OFFSET         0x0034U
#define N800_GPIO_DATAOUT_OFFSET    0x003cU
#define N800_BLIZZARD_PWDN_BIT      15U

#define TAHVO_CBUS_ID               0x02U
#define TAHVO_REG_VCORE             0x07U
#define TAHVO_VCORE_MASK            0x000fU

int n800_display_set_powerdown(bool enable)
{
   void __iomem *gpio1 = IOMEM(N800_GPIO1_BASE);
   u32 dataout;

   if (readl(gpio1 + N800_GPIO_OE_OFFSET) & BIT(N800_BLIZZARD_PWDN_BIT))
      return -EINVAL;

   dataout = readl(gpio1 + N800_GPIO_DATAOUT_OFFSET);
   if (enable)
      dataout &= ~BIT(N800_BLIZZARD_PWDN_BIT);
   else
      dataout |= BIT(N800_BLIZZARD_PWDN_BIT);

   writel(dataout, gpio1 + N800_GPIO_DATAOUT_OFFSET);
   mdelay(20);

   return 0;
}

int n800_display_power_up(void)
{
   int tahvo;
   int ret;

   /* Exact RX-34 blizzard_power_up() sequence used by displaydiag V1.14. */
   tahvo = cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE);
   if (tahvo < 0)
      return tahvo;

   ret = cbus_gpio_write_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE,
                             tahvo & ~TAHVO_VCORE_MASK);
   if (ret)
      return ret;
   mdelay(10);

   omap2_enable_oscillator();
   return 0;
}
