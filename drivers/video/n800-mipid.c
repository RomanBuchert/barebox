// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <errno.h>
#include <mach/omap/omap2-mcspi.h>

#include "n800-mipid.h"

#define MIPID_CMD_READ_DISP_ID 0x04
#define MIPID_CMD_SLEEP_IN 0x10
#define MIPID_CMD_SLEEP_OUT 0x11
#define MIPID_CMD_DISP_OFF 0x28
#define MIPID_CMD_DISP_ON 0x29
#define MIPID_CMD_PIXEL_FORMAT 0x3a
#define MIPID_CMD_NOKIA_INIT 0xc2

#define N800_MIPID_MAX_WRITE_BYTES 8

static const struct omap2_mcspi_device n800_mipid_spi = {
   .chip_select = 1,
   .mode = OMAP2_MCSPI_MODE_1,
   .bits_per_word = 9,
   .max_speed_hz = 4000000,
};

/*
 * MIPID reads keep CS asserted for the complete transaction. The command is
 * sent as a 9-bit word. The first receive word is also 9 bits because its
 * ninth clock is the protocol's extra clock before the first returned byte.
 * Any remaining response bytes are transferred as 8-bit words.
 */
static int n800_mipid_read_internal(u8 command, u8 *response, size_t count,
                                    struct n800_mipid_trace *trace)
{
   struct omap2_mcspi_word words[N800_MIPID_MAX_READ_WORDS];
   u32 local_rx[N800_MIPID_MAX_READ_WORDS];
   u32 *rx = local_rx;
   size_t i;
   int ret;

   if (!response || !count || count > N800_MIPID_MAX_READ_BYTES)
      return -EINVAL;

   words[0].tx = command;
   words[0].bits_per_word = 9;

   for (i = 0; i < count; i++) {
      words[i + 1].tx = 0;
      words[i + 1].bits_per_word = i == 0 ? 9 : 8;
   }

   ret = omap2_mcspi1_setup();
   if (ret)
      return ret;

   if (trace) {
      memset(trace, 0, sizeof(*trace));
      trace->spi.word = trace->word;
      trace->spi.words = count + 1;
      rx = trace->rx;
      ret = omap2_mcspi1_transfer_words_trace(&n800_mipid_spi, words, rx, count + 1,
                                              &trace->spi);
   } else {
      ret = omap2_mcspi1_transfer_words(&n800_mipid_spi, words, rx, count + 1);
   }

   if (ret)
      return ret;

   for (i = 0; i < count; i++)
      response[i] = rx[i + 1] & 0xff;

   return 0;
}

int n800_mipid_read(u8 command, u8 *response, size_t count)
{
   return n800_mipid_read_internal(command, response, count, NULL);
}

int n800_mipid_read_trace(u8 command, u8 *response, size_t count,
                          struct n800_mipid_trace *trace)
{
   if (!trace)
      return -EINVAL;

   return n800_mipid_read_internal(command, response, count, trace);
}

int n800_mipid_read_id(u8 *response, size_t count)
{
   if (count < 3)
      return -EINVAL;

   return n800_mipid_read(MIPID_CMD_READ_DISP_ID, response, 3);
}

int n800_mipid_read_id_trace(u8 *response, size_t count, struct n800_mipid_trace *trace)
{
   if (count < 3)
      return -EINVAL;

   return n800_mipid_read_trace(MIPID_CMD_READ_DISP_ID, response, 3, trace);
}


int n800_mipid_write(u8 command, const u8 *data, size_t count)
{
   struct omap2_mcspi_word words[N800_MIPID_MAX_WRITE_BYTES + 1];
   size_t i;
   int ret;

   if (count > N800_MIPID_MAX_WRITE_BYTES)
      return -EINVAL;
   if (count && !data)
      return -EINVAL;

   /* Bit 8 is the DBI-C D/C bit: 0 = command, 1 = parameter data. */
   words[0].tx = command;
   words[0].bits_per_word = 9;

   for (i = 0; i < count; i++) {
      words[i + 1].tx = 0x100 | data[i];
      words[i + 1].bits_per_word = 9;
   }

   ret = omap2_mcspi1_setup();
   if (ret)
      return ret;

   return omap2_mcspi1_transfer_words(&n800_mipid_spi, words, NULL, count + 1);
}

int n800_mipid_command(u8 command)
{
   return n800_mipid_write(command, NULL, 0);
}

int n800_mipid_reinitialize_ls041y3(void)
{
   /*
    * Nokia lcd_mipid.c sends 9-bit DBI-C parameter words stored as u16:
    *   C2: 0x0102, 0x0100, 0x0100
    *   3A: 0x0170 for 24 data lines
    * n800_mipid_write() supplies bit 8, so pass the low bytes only.
    */
   static const u8 init_string[] = { 0x02, 0x00, 0x00 };
   static const u8 pixel_format[] = { 0x70 };
   int ret;

   /*
    * Mirror Nokia's lcd_mipid disable/enable sequencing. When cycling sleep
    * immediately, the driver's 120 ms hardware guard makes the effective
    * Sleep In -> Sleep Out delay 120 ms. Sleep Out then requires another
    * 120 ms before the initialization string is sent.
    *
    * Nokia obtains data_lines from the NOLO LCD ATAG; 24 lines maps to
    * 9-bit DBI-C data word 0x170 (0x70 with the data bit set).
    * Verify the NOLO configuration before treating this as a cold-boot path.
    */
   ret = n800_mipid_command(MIPID_CMD_DISP_OFF);
   if (ret)
      return ret;

   ret = n800_mipid_command(MIPID_CMD_SLEEP_IN);
   if (ret)
      return ret;

   mdelay(120);

   ret = n800_mipid_command(MIPID_CMD_SLEEP_OUT);
   if (ret)
      return ret;

   mdelay(120);

   ret = n800_mipid_write(MIPID_CMD_NOKIA_INIT, init_string, ARRAY_SIZE(init_string));
   if (ret)
      return ret;

   ret = n800_mipid_write(MIPID_CMD_PIXEL_FORMAT, pixel_format, ARRAY_SIZE(pixel_format));
   if (ret)
      return ret;

   return n800_mipid_command(MIPID_CMD_DISP_ON);
}

/*
 * Nokia's lcd_mipid.c maps 16/18/24 data lines to 9-bit DBI-C
 * parameters 0x150/0x160/0x170. Change only the panel data-line
 * setting; do not cycle sleep or disturb the Blizzard/RFBI path.
 */
int n800_mipid_set_data_lines(unsigned int lines)
{
   u8 value;

   switch (lines) {
      case 16:
         value = 0x50;
         break;
      case 18:
         value = 0x60;
         break;
      case 24:
         value = 0x70;
         break;
      default:
         return -EINVAL;
   }

   return n800_mipid_write(MIPID_CMD_PIXEL_FORMAT, &value, 1);
}
