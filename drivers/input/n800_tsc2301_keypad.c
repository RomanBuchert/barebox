// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <driver.h>
#include <errno.h>
#include <init.h>
#include <input/input.h>
#include <mach/omap/omap2-mcspi.h>
#include <poller.h>

#define TSC2301_READ                      BIT(15)
#define TSC2301_PAGE_DATA                 0
#define TSC2301_PAGE_CONTROL              1
#define TSC2301_REG_KPDATA                0x04
#define TSC2301_REG_KEY                   0x01
#define TSC2301_REG_CONFIG2               0x06
#define TSC2301_REG_KPMASK                0x10
#define TSC2301_COMMAND(read, page, reg)  ((read) | ((page) << 11) | ((reg) << 5))

#define TSC2301_KEY_STOP                  0x4000
#define TSC2301_KEY_DEBOUNCE_20MS         0x1000
#define TSC2301_CONFIG2_KBC_SHIFT         14
#define TSC2301_CONFIG2_KBC_MASK          (0x3 << TSC2301_CONFIG2_KBC_SHIFT)
#define TSC2301_CONFIG2_KBC_MODE2         (0x2 << TSC2301_CONFIG2_KBC_SHIFT)
#define TSC2301_KEYPAD_MASK_UNUSED        0x8889

#define N800_TSC2301_POLL_INTERVAL_NS     (20ULL * 1000ULL * 1000ULL)

struct n800_tsc2301_keypad {
   struct input_device input;
   struct poller_async poller;
   u16 previous;
};

static const unsigned int n800_keycodes[16] = {
   [1] = KEY_UP,
   [2] = KEY_HOME,
   [4] = KEY_LEFT,
   [5] = KEY_ENTER,
   [6] = KEY_RIGHT,
   [8] = KEY_ESC,
   [9] = KEY_DOWN,
   [10] = KEY_MENU,
   [12] = KEY_ZOOMOUT,
   [13] = KEY_FULL_SCREEN,
   [14] = KEY_ZOOMIN,
};

static const struct omap2_mcspi_device n800_tsc2301_spi = {
   .chip_select = 0,
   .mode = OMAP2_MCSPI_MODE_0,
   .bits_per_word = 16,
   .max_speed_hz = 6000000,
};

static int n800_tsc2301_transfer(struct n800_tsc2301_keypad *keypad, u16 command,
                                 u16 tx, u16 *rx)
{
   u32 tx_words[2] = { command, tx };
   u32 rx_words[2];
   int ret;

   ret = omap2_mcspi1_transfer(&n800_tsc2301_spi, tx_words, rx_words,
                               ARRAY_SIZE(tx_words));
   if (ret)
      return ret;

   *rx = rx_words[1] & 0xffff;

   return 0;
}

static int n800_tsc2301_read_reg(struct n800_tsc2301_keypad *keypad, u8 page, u8 reg,
                                 u16 *value)
{
   return n800_tsc2301_transfer(keypad, TSC2301_COMMAND(TSC2301_READ, page, reg),
                                0, value);
}

static int n800_tsc2301_write_reg(struct n800_tsc2301_keypad *keypad, u8 page, u8 reg,
                                  u16 value)
{
   u16 dummy;

   return n800_tsc2301_transfer(keypad, TSC2301_COMMAND(0, page, reg), value, &dummy);
}

static int n800_tsc2301_keypad_init(struct n800_tsc2301_keypad *keypad, u16 *state)
{
   u16 config2;
   int ret;

   ret = n800_tsc2301_write_reg(keypad, TSC2301_PAGE_CONTROL, TSC2301_REG_KEY,
                                TSC2301_KEY_STOP);
   if (ret)
      return ret;

   ret = n800_tsc2301_read_reg(keypad, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG2,
                               &config2);
   if (ret)
      return ret;

   config2 &= ~TSC2301_CONFIG2_KBC_MASK;
   config2 |= TSC2301_CONFIG2_KBC_MODE2;

   ret = n800_tsc2301_write_reg(keypad, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG2,
                                config2);
   if (ret)
      return ret;

   ret = n800_tsc2301_write_reg(keypad, TSC2301_PAGE_CONTROL, TSC2301_REG_KPMASK,
                                TSC2301_KEYPAD_MASK_UNUSED);
   if (ret)
      return ret;

   ret = n800_tsc2301_write_reg(keypad, TSC2301_PAGE_CONTROL, TSC2301_REG_KEY,
                                TSC2301_KEY_DEBOUNCE_20MS);
   if (ret)
      return ret;

   return n800_tsc2301_read_reg(keypad, TSC2301_PAGE_DATA, TSC2301_REG_KPDATA, state);
}

static void n800_tsc2301_report(struct n800_tsc2301_keypad *keypad, u16 state)
{
   u16 changed;
   unsigned int bit;

#ifdef CONFIG_KEYBOARD_N800_TSC2301_DEBUG
   if ((state & ~TSC2301_KEYPAD_MASK_UNUSED) != keypad->previous)
      printf("N800 keypad: KPDATA=0x%04x previous=0x%04x\n", state, keypad->previous);
#endif

   state &= ~TSC2301_KEYPAD_MASK_UNUSED;
   changed = state ^ keypad->previous;

   if (!changed)
      return;

   for (bit = 0; bit < ARRAY_SIZE(n800_keycodes); bit++) {
      if (!(changed & BIT(bit)) || !n800_keycodes[bit])
         continue;

#ifdef CONFIG_KEYBOARD_N800_TSC2301_DEBUG
      printf("N800 keypad: key=%u value=%u bit=%u\n", n800_keycodes[bit],
             !!(state & BIT(bit)), bit);
#endif
      input_report_key_event(&keypad->input, n800_keycodes[bit],
                             !!(state & BIT(bit)));
   }

   keypad->previous = state;
}

static void n800_tsc2301_poll(void *ctx)
{
   struct n800_tsc2301_keypad *keypad = ctx;
   u16 state;

   if (!n800_tsc2301_read_reg(keypad, TSC2301_PAGE_DATA, TSC2301_REG_KPDATA, &state))
      n800_tsc2301_report(keypad, state);

   poller_call_async(&keypad->poller, N800_TSC2301_POLL_INTERVAL_NS,
                     n800_tsc2301_poll, keypad);
}

static int n800_tsc2301_keypad_probe(struct device *dev)
{
   struct n800_tsc2301_keypad *keypad;
   u16 state;
   int ret;

   keypad = xzalloc(sizeof(*keypad));
   keypad->input.parent = dev;

   ret = omap2_mcspi1_setup();
   if (ret) {
      dev_err(dev, "failed to set up McSPI1: %pe\n", ERR_PTR(ret));
      return ret;
   }

   ret = n800_tsc2301_keypad_init(keypad, &state);
   if (ret) {
      dev_err(dev, "failed to initialize TSC2301 keypad: %pe\n", ERR_PTR(ret));
      return ret;
   }

   keypad->previous = state & ~TSC2301_KEYPAD_MASK_UNUSED;

   ret = input_device_register(&keypad->input);
   if (ret)
      return ret;

   ret = poller_async_register(&keypad->poller, dev_name(dev));
   if (ret) {
      input_device_unregister(&keypad->input);
      return ret;
   }

   ret = poller_call_async(&keypad->poller, N800_TSC2301_POLL_INTERVAL_NS,
                           n800_tsc2301_poll, keypad);
   if (ret) {
      poller_async_unregister(&keypad->poller);
      input_device_unregister(&keypad->input);
      return ret;
   }

   dev_info(dev, "TSC2301 keypad active, KPDATA=0x%04x\n", keypad->previous);

   return 0;
}

static const struct of_device_id n800_tsc2301_keypad_dt_ids[] = {
   { .compatible = "n8x0s,n800-tsc2301-keypad" },
   { }
};
MODULE_DEVICE_TABLE(of, n800_tsc2301_keypad_dt_ids);

static struct driver n800_tsc2301_keypad_driver = {
   .name = "n800-tsc2301-keypad",
   .probe = n800_tsc2301_keypad_probe,
   .of_compatible = DRV_OF_COMPAT(n800_tsc2301_keypad_dt_ids),
};
device_platform_driver(n800_tsc2301_keypad_driver);
