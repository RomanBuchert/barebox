// SPDX-License-Identifier: GPL-2.0-only
/* Nokia N800 transport for the TI TSC2301 on OMAP2420 McSPI1 CS0. */

#include <common.h>
#include <errno.h>
#include <input/n800-tsc2301.h>
#include <mach/omap/omap2-mcspi.h>

static const struct omap2_mcspi_device n800_tsc2301_spi = {
   .chip_select = 0,
   .mode = N800_TSC2301_SPI_MODE,
   .bits_per_word = N800_TSC2301_SPI_BITS,
   .max_speed_hz = N800_TSC2301_SPI_MAX_SPEED_HZ,
};

static int n800_tsc2301_transfer(void *context, u16 command, u16 tx, u16 *rx)
{
   u32 tx_words[2] = { command, tx };
   u32 rx_words[2];
   int ret;

   (void)context;

   /*
    * McSPI1 is shared with the LCD panel on CS1. The transfer helper applies
    * this device configuration immediately before asserting CS0, so no state
    * left by a display transfer is assumed here.
    */
   ret = omap2_mcspi1_transfer(&n800_tsc2301_spi, tx_words, rx_words,
                               ARRAY_SIZE(tx_words));
   if (ret)
      return ret;

   if (rx)
      *rx = rx_words[1] & 0xffffU;

   return 0;
}

int n800_tsc2301_init(struct tsc2301 *tsc)
{
   int ret;

   if (!tsc)
      return -EINVAL;

   ret = omap2_mcspi1_setup();
   if (ret)
      return ret;

   tsc->context = NULL;
   tsc->transfer = n800_tsc2301_transfer;

   return 0;
}
