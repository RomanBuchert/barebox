// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <errno.h>
#include <mach/omap/omap2-mcspi.h>

#include "n800-mipid.h"

#define MIPID_CMD_READ_DISP_ID 0x04

static const struct omap2_mcspi_device n800_mipid_spi = {
   .chip_select = 1,
   .mode = OMAP2_MCSPI_MODE_1,
   .bits_per_word = 9,
   .max_speed_hz = 4000000,
};

/*
 * Match the original Nokia lcd_mipid.c transfer exactly for a 3-byte read:
 * one 9-bit command word, one 9-bit receive word (the ninth clock is the
 * protocol's extra clock before the first data bit), then two 8-bit words.
 */
static const struct omap2_mcspi_word n800_mipid_id_words[] = {
   { .tx = MIPID_CMD_READ_DISP_ID, .bits_per_word = 9 },
   { .tx = 0, .bits_per_word = 9 },
   { .tx = 0, .bits_per_word = 8 },
   { .tx = 0, .bits_per_word = 8 },
};

static int n800_mipid_read_id_internal(u8 *response, size_t count,
                                      struct n800_mipid_id_trace *trace)
{
   u32 local_rx[ARRAY_SIZE(n800_mipid_id_words)];
   u32 *rx = local_rx;
   int ret;

   if (!response || count < 3)
      return -EINVAL;

   ret = omap2_mcspi1_setup();
   if (ret)
      return ret;

   if (trace) {
      memset(trace, 0, sizeof(*trace));
      trace->spi.word = trace->word;
      trace->spi.words = ARRAY_SIZE(trace->word);
      rx = trace->rx;
      ret = omap2_mcspi1_transfer_words_trace(&n800_mipid_spi, n800_mipid_id_words, rx,
                                              ARRAY_SIZE(n800_mipid_id_words), &trace->spi);
   } else {
      ret = omap2_mcspi1_transfer_words(&n800_mipid_spi, n800_mipid_id_words, rx,
                                        ARRAY_SIZE(n800_mipid_id_words));
   }

   if (ret)
      return ret;

   response[0] = rx[1] & 0xff;
   response[1] = rx[2] & 0xff;
   response[2] = rx[3] & 0xff;

   return 0;
}

int n800_mipid_read_id(u8 *response, size_t count)
{
   return n800_mipid_read_id_internal(response, count, NULL);
}

int n800_mipid_read_id_trace(u8 *response, size_t count, struct n800_mipid_id_trace *trace)
{
   if (!trace)
      return -EINVAL;

   return n800_mipid_read_id_internal(response, count, trace);
}
