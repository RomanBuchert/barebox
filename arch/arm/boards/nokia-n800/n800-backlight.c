// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <command.h>
#include <errno.h>
#include <i2c/i2c-cbus-gpio.h>
#include <linux/kernel.h>

#include "n800-backlight.h"

#define BETTY_CBUS_ADDRESS 0x02U
#define BETTY_REG_LED_PWM  0x05U

int n800_backlight_get_brightness(unsigned int *brightness)
{
   int value;

   if (!brightness)
      return -EINVAL;

   value = cbus_gpio_read_reg(BETTY_CBUS_ADDRESS, BETTY_REG_LED_PWM);
   if (value < 0)
      return value;

   *brightness = (unsigned int)value & N800_BACKLIGHT_MAX;
   return 0;
}

int n800_backlight_set_brightness(unsigned int brightness)
{
   if (brightness > N800_BACKLIGHT_MAX)
      return -ERANGE;

   return cbus_gpio_write_reg(BETTY_CBUS_ADDRESS, BETTY_REG_LED_PWM, brightness);
}

static int n800_backlight_print(void)
{
   unsigned int brightness;
   int ret;

   ret = n800_backlight_get_brightness(&brightness);
   if (ret) {
      printf("n800backlight: failed to read BackgroundLED brightness: %s\n",
             strerror(-ret));
      return COMMAND_ERROR;
   }

   printf("BackgroundLED brightness: %u/%u\n", brightness, N800_BACKLIGHT_MAX);
   return 0;
}

static int do_n800backlight(int argc, char *argv[])
{
   unsigned int brightness;
   int ret;

   if (argc == 1)
      return n800_backlight_print();

   if (argc != 2)
      return COMMAND_ERROR_USAGE;

   ret = kstrtouint(argv[1], 0, &brightness);
   if (ret || brightness > N800_BACKLIGHT_MAX)
      return COMMAND_ERROR_USAGE;

   ret = n800_backlight_set_brightness(brightness);
   if (ret) {
      printf("n800backlight: failed to set BackgroundLED brightness: %s\n",
             strerror(-ret));
      return COMMAND_ERROR;
   }

   /* Always report the value read back from Betty, never a cached value. */
   return n800_backlight_print();
}

BAREBOX_CMD_START(n800backlight)
   .cmd = do_n800backlight,
   BAREBOX_CMD_DESC("get or set Nokia N800 BackgroundLED brightness")
   BAREBOX_CMD_OPTS("[0..127]")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
BAREBOX_CMD_END
