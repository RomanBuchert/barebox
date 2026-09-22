// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <i2c/i2c-cbus-gpio.h>
#include <mach/omap/omap2-clock.h>

#include "n800-display-power.h"

#define TAHVO_CBUS_ID               0x02U
#define TAHVO_REG_VCORE             0x07U
#define TAHVO_VCORE_MASK            0x000fU

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

   /*
    * Do not remux or rewrite GPIO15 here.  V1.14 deliberately preserved the
    * NOLO-provided POWERDOWN pin state and that exact path is verified on the
    * real N800.  A future cold-init/NOLO-replacement path belongs separately.
    */
   return 0;
}
