// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <command.h>
#include <errno.h>

#include "../drivers/video/n800-mipid.h"

static int do_n8x0s_mipid_test(int argc, char *argv[])
{
   struct n800_mipid_id_trace trace;
   u8 id[3];
   size_t i;
   int ret;

   ret = n800_mipid_read_id_trace(id, ARRAY_SIZE(id), &trace);
   if (ret) {
      printf("N800 MIPID CS1 read failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   printf("N800 MIPID McSPI1/CS1 trace:\n");
   printf("  MODULCTRL=%08x CHCONF1(initial)=%08x CHCTRL1(initial)=%08x\n",
          trace.spi.modulctrl, trace.spi.chconf_initial, trace.spi.chctrl_initial);

   for (i = 0; i < trace.spi.words; i++) {
      const struct omap2_mcspi_trace_word *word = &trace.word[i];

      printf("  [%zu] WL=%u TX=%08x RX=%08x CHCONF=%08x CHCTRL=%08x "
             "CHSTAT(before/after)=%08x/%08x\n",
             i, word->bits_per_word, word->tx, word->rx, word->chconf, word->chctrl,
             word->chstat_before, word->chstat_after);
   }

   printf("N800 MIPID display ID: %02x %02x %02x\n", id[0], id[1], id[2]);
   switch (id[0]) {
      case 0x45:
         printf("Nokia panel: LPH8923, revision %02x\n", id[1]);
         break;
      case 0x83:
         printf("Nokia panel: LS041Y3, revision %02x\n", id[1]);
         break;
      default:
         printf("Unknown panel ID; trace captured, no panel state changed.\n");
         break;
   }

   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_mipid_test)
BAREBOX_CMD_HELP_TEXT("Read the LCD MIPID display ID and dump McSPI1/CS1 framing.")
BAREBOX_CMD_HELP_TEXT("Read-only; does not change panel state.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_mipid_test)
   .cmd = do_n8x0s_mipid_test,
   BAREBOX_CMD_DESC("trace N800 LCD MIPID McSPI1 CS1 read")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_mipid_test_help)
BAREBOX_CMD_END
