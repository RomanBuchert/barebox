// SPDX-License-Identifier: GPL-2.0-only
/* Known-good TSC2301 core diagnostic for the real Nokia N800.
 *
 * This deliberately tests only generic register access. Keypad, touchscreen
 * and audio initialization belong to their respective device layers.
 */

#include <command.h>
#include <common.h>
#include <errno.h>
#include <input/n800-tsc2301.h>
#include <input/tsc2301.h>
#include <mach/omap/omap2-mcspi.h>

static void n800_tsc2301_print_mcspi(const char *label)
{
   struct omap2_mcspi_channel_state state;

   if (omap2_mcspi1_get_channel_state(0, &state))
      return;

   printf("%s\n", label);
   printf("  MODULCTRL: %08x\n", state.modulctrl);
   printf("  CHCONF0  : %08x\n", state.chconf);
   printf("  CHSTAT0  : %08x\n", state.chstat);
   printf("  CHCTRL0  : %08x\n", state.chctrl);
}

static int n800_tsc2301_prepare(struct tsc2301 *tsc)
{
   int ret;

   memset(tsc, 0, sizeof(*tsc));
   ret = n800_tsc2301_init(tsc);
   if (ret)
      printf("TSC2301 transport setup failed: %pe\n", ERR_PTR(ret));

   return ret;
}


static int n800_tsc2301_core_read_stability(struct tsc2301 *tsc, u8 page, u8 reg,
                                             const char *name, u16 *value)
{
   unsigned int sample;
   u16 current;
   int ret;

   ret = tsc2301_read_reg(tsc, page, reg, value);
   if (ret)
      return ret;

   printf("  %-10s:", name);
   printf(" %04x", *value);

   for (sample = 1; sample < 8; sample++) {
      ret = tsc2301_read_reg(tsc, page, reg, &current);
      if (ret) {
         printf(" read failed: %pe\n", ERR_PTR(ret));
         return ret;
      }

      printf(" %04x", current);
      if (current != *value) {
         printf("  FAIL (unstable)\n");
         return -EIO;
      }
   }

   printf("  stable\n");
   return 0;
}

static int do_n8x0s_tsc2301_core_test(int argc, char *argv[])
{
   struct tsc2301 tsc;
   u16 reserved_data;
   u16 reset;
   u16 config;
   u16 reserved_control;
   u16 test_value;
   u16 readback;
   u16 restored;
   bool readback_valid = false;
   int ret;

   ret = n800_tsc2301_prepare(&tsc);
   if (ret)
      return COMMAND_ERROR;

   printf("N800 TSC2301 core register test\n");
   printf("SPI: McSPI1 CS0, mode 0, 16 bits, max 6 MHz\n");
   printf("McSPI channel configuration is applied immediately before every transfer.\n");
   printf("No keypad, ADC or audio setup is performed.\n");
   printf("The CONFIG write test changes SNS0 briefly and restores the original value.\n");
   n800_tsc2301_print_mcspi("McSPI1 CS0 before core test:");

   printf("Read stability (8 reads each):\n");
   ret = n800_tsc2301_core_read_stability(&tsc, TSC2301_PAGE_DATA,
                                           TSC2301_REG_RESERVED0C,
                                           "P0 reserved", &reserved_data);
   if (ret)
      goto failed;

   ret = n800_tsc2301_core_read_stability(&tsc, TSC2301_PAGE_CONTROL,
                                           TSC2301_REG_RESET, "RESET", &reset);
   if (ret)
      goto failed;

   ret = n800_tsc2301_core_read_stability(&tsc, TSC2301_PAGE_CONTROL,
                                           TSC2301_REG_CONFIG, "CONFIG", &config);
   if (ret)
      goto failed;

   ret = n800_tsc2301_core_read_stability(&tsc, TSC2301_PAGE_CONTROL,
                                           TSC2301_REG_RESERVED07,
                                           "P1 reserved", &reserved_control);
   if (ret)
      goto failed;

   printf("Register signatures from TI SLAS371D:\n");
   printf("  P0 reserved 0x0c: 0x%04x, expected 0xffff: %s\n", reserved_data,
          reserved_data == 0xffffU ? "PASS" : "FAIL");
   printf("  RESET       0x04: 0x%04x, expected 0xffff on read: %s\n", reset,
          reset == 0xffffU ? "PASS" : "FAIL");
   printf("  CONFIG      0x05: 0x%04x, reserved[15:6] expected 1: %s\n", config,
          (config & TSC2301_CONFIG_RESERVED_MASK) == TSC2301_CONFIG_RESERVED_MASK ?
             "PASS" : "FAIL");
   printf("  P1 reserved 0x07: 0x%04x, expected 0xffff: %s\n", reserved_control,
          reserved_control == 0xffffU ? "PASS" : "FAIL");

   if (reserved_data != 0xffffU || reset != 0xffffU ||
       (config & TSC2301_CONFIG_RESERVED_MASK) != TSC2301_CONFIG_RESERVED_MASK ||
       reserved_control != 0xffffU) {
      printf("Core read signature: FAIL; write test skipped.\n");
      goto failed;
   }

   test_value = config ^ BIT(0);
   printf("Reversible CONFIG write/readback test:\n");
   printf("  original : 0x%04x\n", config);
   printf("  test     : 0x%04x (toggle SNS0 only)\n", test_value);

   ret = tsc2301_write_reg(&tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG,
                           test_value);
   if (ret)
      goto restore_failed;

   ret = tsc2301_read_reg(&tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG,
                          &readback);
   if (ret)
      goto restore_original;
   readback_valid = true;
   printf("  readback : 0x%04x  %s\n", readback,
          readback == test_value ? "PASS" : "FAIL");

restore_original:
   ret = tsc2301_write_reg(&tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG, config);
   if (ret)
      goto restore_failed;

   ret = tsc2301_read_reg(&tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG,
                          &restored);
   if (ret)
      goto failed;
   printf("  restored : 0x%04x  %s\n", restored,
          restored == config ? "PASS" : "FAIL");

   n800_tsc2301_print_mcspi("McSPI1 CS0 after core test:");
   if (!readback_valid || readback != test_value || restored != config) {
      printf("TSC2301 core: FAIL\n");
      return COMMAND_ERROR;
   }

   printf("TSC2301 core: PASS\n");
   return 0;

restore_failed:
   printf("CONFIG restore failed: %pe\n", ERR_PTR(ret));
   printf("WARNING: original CONFIG value may not have been restored.\n");
   return COMMAND_ERROR;

failed:
   n800_tsc2301_print_mcspi("McSPI1 CS0 after failed core test:");
   printf("TSC2301 core: FAIL\n");
   return COMMAND_ERROR;
}

BAREBOX_CMD_START(n8x0s_tsc2301_core_test)
   .cmd = do_n8x0s_tsc2301_core_test,
   BAREBOX_CMD_DESC("test N800 TSC2301 core register access")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
BAREBOX_CMD_END
