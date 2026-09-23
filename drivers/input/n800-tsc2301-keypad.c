// SPDX-License-Identifier: GPL-2.0-only
/* Nokia N800 keypad client for the TI TSC2301. */

#include <clock.h>
#include <common.h>
#include <driver.h>
#include <errno.h>
#include <gpio.h>
#include <init.h>
#include <input/input.h>
#include <input/n800-tsc2301.h>
#include <input/tsc2301.h>
#include <io.h>
#include <poller.h>

#define N800_TSC2301_KBIRQ_GPIO           109U
#define N800_OMAP2420_GPIO4_BASE          0x4801e000U
#define OMAP2_GPIO_IRQSTATUS1             0x0018U
#define OMAP2_GPIO_IRQENABLE1             0x001cU
#define OMAP2_GPIO_FALLINGDETECT          0x004cU
#define N800_TSC2301_KBIRQ_GPIO4_BIT      13U
#define N800_TSC2301_KBIRQ_GPIO4_MASK     BIT(N800_TSC2301_KBIRQ_GPIO4_BIT)
#define N800_TSC2301_KEY_ACTIVE           BIT(15)
#define N800_TSC2301_RELEASE_TIMEOUT_NS    (50ULL * MSECOND)

struct n800_tsc2301_keypad {
   struct input_device input;
   struct poller_struct poller;
   struct tsc2301 tsc;
   void __iomem *gpio4;
   u64 last_event_ns;
   u16 pressed;
};

static const unsigned int n800_tsc2301_keycodes[16] = {
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

static int n800_tsc2301_keypad_read(struct n800_tsc2301_keypad *keypad, u16 *key,
                                     u16 *kpdata)
{
   int ret;

   ret = tsc2301_read_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_KEY, key);
   if (ret)
      return ret;

   return tsc2301_read_reg(&keypad->tsc, TSC2301_PAGE_DATA, TSC2301_REG_KPDATA,
                           kpdata);
}

static int n800_tsc2301_keypad_hw_init(struct n800_tsc2301_keypad *keypad)
{
   u16 config2;
   u16 readback;
   int ret;

   ret = tsc2301_read_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG2,
                          &config2);
   if (ret)
      return ret;

   ret = tsc2301_write_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_KPMASK,
                           TSC2301_N800_KEYPAD_MASK);
   if (ret)
      return ret;

   ret = tsc2301_write_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_KEY,
                           TSC2301_KEY_DEBOUNCE_20MS);
   if (ret)
      return ret;

   config2 &= ~TSC2301_CONFIG2_KBC_MASK;
   config2 |= TSC2301_CONFIG2_KBC_MODE2;
   ret = tsc2301_write_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG2,
                           config2);
   if (ret)
      return ret;

   ret = tsc2301_write_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_KPMASK,
                           TSC2301_N800_KEYPAD_MASK);
   if (ret)
      return ret;

   ret = tsc2301_read_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_CONFIG2,
                          &readback);
   if (ret)
      return ret;
   if ((readback & TSC2301_CONFIG2_KBC_MASK) != TSC2301_CONFIG2_KBC_MODE2)
      return -EIO;

   ret = tsc2301_read_reg(&keypad->tsc, TSC2301_PAGE_CONTROL, TSC2301_REG_KPMASK,
                          &readback);
   if (ret)
      return ret;
   if (readback != TSC2301_N800_KEYPAD_MASK)
      return -EIO;

   return 0;
}

static void n800_tsc2301_report_state(struct n800_tsc2301_keypad *keypad, u16 state)
{
   u16 changed = keypad->pressed ^ state;
   unsigned int bit;

   if (!changed)
      return;

   for (bit = 0; bit < ARRAY_SIZE(n800_tsc2301_keycodes); bit++) {
      unsigned int code = n800_tsc2301_keycodes[bit];

      if (!code || !(changed & BIT(bit)))
         continue;

      input_report_key_event(&keypad->input, code, !!(state & BIT(bit)));

      if (IS_ENABLED(CONFIG_KEYBOARD_N800_TSC2301_DEBUG))
         dev_info(keypad->input.parent, "key %u %s (KPDATA bit %u)\n", code,
                  state & BIT(bit) ? "pressed" : "released", bit);
   }

   keypad->pressed = state;
}

static void n800_tsc2301_keypad_poller(struct poller_struct *poller)
{
   struct n800_tsc2301_keypad *keypad =
      container_of(poller, struct n800_tsc2301_keypad, poller);
   u32 status;
   u16 key;
   u16 kpdata;
   u16 state;
   int ret;

   status = readl(keypad->gpio4 + OMAP2_GPIO_IRQSTATUS1);
   if (status & N800_TSC2301_KBIRQ_GPIO4_MASK) {
      /*
       * Clear the OMAP edge latch before the SPI transaction. A new falling
       * edge that occurs while KEY/KPDATA is read then remains pending.
       */
      writel(N800_TSC2301_KBIRQ_GPIO4_MASK, keypad->gpio4 + OMAP2_GPIO_IRQSTATUS1);

      ret = n800_tsc2301_keypad_read(keypad, &key, &kpdata);
      if (ret) {
         dev_err(keypad->input.parent, "failed to read keypad event: %pe\n",
                 ERR_PTR(ret));
         return;
      }

      state = key & N800_TSC2301_KEY_ACTIVE ? kpdata : 0;
      state &= ~TSC2301_N800_KEYPAD_MASK;

      n800_tsc2301_report_state(keypad, state);
      keypad->last_event_ns = get_time_ns();
      return;
   }

   /*
    * Real RX-34 hardware repeats KBIRQ about every 36.2 ms while a key is
    * held, but generates no falling-edge KBIRQ on release. No TSC register is
    * polled here: absence of the hardware heartbeat for 50 ms is the release.
    */
   if (keypad->pressed &&
       is_timeout(keypad->last_event_ns, N800_TSC2301_RELEASE_TIMEOUT_NS))
      n800_tsc2301_report_state(keypad, 0);
}

static int n800_tsc2301_keypad_probe(struct device *dev)
{
   struct n800_tsc2301_keypad *keypad;
   u32 falling;
   u32 irqenable;
   u16 key;
   u16 kpdata;
   int ret;

   keypad = xzalloc(sizeof(*keypad));
   keypad->input.parent = dev;
   keypad->gpio4 = IOMEM(N800_OMAP2420_GPIO4_BASE);

   ret = n800_tsc2301_init(&keypad->tsc);
   if (ret) {
      dev_err(dev, "failed to initialize TSC2301 transport: %pe\n", ERR_PTR(ret));
      return ret;
   }

   ret = n800_tsc2301_keypad_hw_init(keypad);
   if (ret) {
      dev_err(dev, "failed to initialize TSC2301 keypad: %pe\n", ERR_PTR(ret));
      return ret;
   }

   ret = gpio_direction_input(N800_TSC2301_KBIRQ_GPIO);
   if (ret) {
      dev_err(dev, "failed to configure GPIO109 as input: %pe\n", ERR_PTR(ret));
      return ret;
   }

   /* Reading KPDATA acknowledges any stale KBIRQ before edge detection starts. */
   ret = n800_tsc2301_keypad_read(keypad, &key, &kpdata);
   if (ret) {
      dev_err(dev, "failed to clear initial keypad state: %pe\n", ERR_PTR(ret));
      return ret;
   }

   irqenable = readl(keypad->gpio4 + OMAP2_GPIO_IRQENABLE1);
   writel(irqenable & ~N800_TSC2301_KBIRQ_GPIO4_MASK,
          keypad->gpio4 + OMAP2_GPIO_IRQENABLE1);
   writel(N800_TSC2301_KBIRQ_GPIO4_MASK, keypad->gpio4 + OMAP2_GPIO_IRQSTATUS1);

   falling = readl(keypad->gpio4 + OMAP2_GPIO_FALLINGDETECT);
   writel(falling | N800_TSC2301_KBIRQ_GPIO4_MASK,
          keypad->gpio4 + OMAP2_GPIO_FALLINGDETECT);

   ret = input_device_register(&keypad->input);
   if (ret)
      return ret;

   keypad->poller.func = n800_tsc2301_keypad_poller;
   ret = poller_register(&keypad->poller, dev_name(dev));
   if (ret) {
      input_device_unregister(&keypad->input);
      return ret;
   }

   dev_info(dev, "TSC2301 keypad active on GPIO109, 50 ms release timeout\n");
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
