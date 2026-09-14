// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <driver.h>
#include <errno.h>
#include <init.h>
#include <input/input.h>
#include <io.h>
#include <poller.h>

#define N800_MCSPI1_BASE               0x48098000

#define MCSPI_SYSCONFIG                 0x10
#define MCSPI_SYSSTATUS                 0x14
#define MCSPI_MODULCTRL                 0x28
#define MCSPI_CHCONF0                   0x2c
#define MCSPI_CHSTAT0                   0x30
#define MCSPI_CHCTRL0                   0x34
#define MCSPI_TX0                       0x38
#define MCSPI_RX0                       0x3c

#define MCSPI_SYSCONFIG_SOFTRESET       BIT(1)
#define MCSPI_SYSSTATUS_RESETDONE       BIT(0)
#define MCSPI_MODULCTRL_SINGLE          BIT(0)
#define MCSPI_MODULCTRL_MS              BIT(2)
#define MCSPI_MODULCTRL_STEST           BIT(3)

#define MCSPI_CHCONF_PHA                BIT(0)
#define MCSPI_CHCONF_POL                BIT(1)
#define MCSPI_CHCONF_CLKD_SHIFT         2
#define MCSPI_CHCONF_CLKD_MASK          (0xf << MCSPI_CHCONF_CLKD_SHIFT)
#define MCSPI_CHCONF_EPOL               BIT(6)
#define MCSPI_CHCONF_WL_SHIFT           7
#define MCSPI_CHCONF_WL_MASK            (0x1f << MCSPI_CHCONF_WL_SHIFT)
#define MCSPI_CHCONF_TRM_MASK           (0x3 << 12)
#define MCSPI_CHCONF_DPE0               BIT(16)
#define MCSPI_CHCONF_DPE1               BIT(17)
#define MCSPI_CHCONF_IS                 BIT(18)
#define MCSPI_CHCONF_FORCE              BIT(20)

#define MCSPI_CHSTAT_RXS                BIT(0)
#define MCSPI_CHSTAT_TXS                BIT(1)
#define MCSPI_CHSTAT_EOT                BIT(2)
#define MCSPI_CHCTRL_EN                 BIT(0)

#define TSC2301_READ                     BIT(15)
#define TSC2301_PAGE_DATA                0
#define TSC2301_REG_KPDATA               0x04
#define TSC2301_COMMAND(page, reg)       (TSC2301_READ | ((page) << 11) | ((reg) << 5))

#define N800_TSC2301_POLL_INTERVAL_NS     (20ULL * 1000ULL * 1000ULL)
#define N800_MCSPI_WAIT_ITERATIONS        4096
#define N800_MCSPI_RESET_ITERATIONS       4096

struct n800_tsc2301_keypad {
   void __iomem *regs;
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

static int n800_mcspi_wait(struct n800_tsc2301_keypad *keypad, u32 mask)
{
   unsigned int count;

   for (count = 0; count < N800_MCSPI_WAIT_ITERATIONS; count++) {
      if (readl(keypad->regs + MCSPI_CHSTAT0) & mask)
         return 0;
   }

   return -ETIMEDOUT;
}

static int n800_mcspi_word(struct n800_tsc2301_keypad *keypad, u16 tx, u16 *rx)
{
   int ret;

   ret = n800_mcspi_wait(keypad, MCSPI_CHSTAT_TXS);
   if (ret)
      return ret;

   writel(tx, keypad->regs + MCSPI_TX0);

   ret = n800_mcspi_wait(keypad, MCSPI_CHSTAT_RXS);
   if (ret)
      return ret;

   *rx = readl(keypad->regs + MCSPI_RX0) & 0xffff;

   return 0;
}

static int n800_tsc2301_read_kpdata(struct n800_tsc2301_keypad *keypad, u16 *value)
{
   u16 dummy;
   u32 conf;
   int ret;

   conf = readl(keypad->regs + MCSPI_CHCONF0);
   conf |= MCSPI_CHCONF_FORCE;
   writel(conf, keypad->regs + MCSPI_CHCONF0);
   writel(MCSPI_CHCTRL_EN, keypad->regs + MCSPI_CHCTRL0);

   ret = n800_mcspi_word(keypad, TSC2301_COMMAND(TSC2301_PAGE_DATA, TSC2301_REG_KPDATA),
                         &dummy);
   if (!ret)
      ret = n800_mcspi_word(keypad, 0, value);

   conf &= ~MCSPI_CHCONF_FORCE;
   writel(conf, keypad->regs + MCSPI_CHCONF0);
   writel(0, keypad->regs + MCSPI_CHCTRL0);

   return ret;
}

static void n800_tsc2301_report(struct n800_tsc2301_keypad *keypad, u16 state)
{
   u16 changed = state ^ keypad->previous;
   unsigned int bit;

   if (!changed)
      return;

   for (bit = 0; bit < ARRAY_SIZE(n800_keycodes); bit++) {
      if (!(changed & BIT(bit)) || !n800_keycodes[bit])
         continue;

      input_report_key_event(&keypad->input, n800_keycodes[bit], !!(state & BIT(bit)));
   }

   keypad->previous = state;
}

static void n800_tsc2301_poll(void *ctx)
{
   struct n800_tsc2301_keypad *keypad = ctx;
   u16 state;

   if (!n800_tsc2301_read_kpdata(keypad, &state))
      n800_tsc2301_report(keypad, state);

   poller_call_async(&keypad->poller, N800_TSC2301_POLL_INTERVAL_NS,
                     n800_tsc2301_poll, keypad);
}

static int n800_mcspi_init(struct n800_tsc2301_keypad *keypad)
{
   unsigned int count;
   u32 conf;

   writel(MCSPI_SYSCONFIG_SOFTRESET, keypad->regs + MCSPI_SYSCONFIG);

   for (count = 0; count < N800_MCSPI_RESET_ITERATIONS; count++) {
      if (readl(keypad->regs + MCSPI_SYSSTATUS) & MCSPI_SYSSTATUS_RESETDONE)
         break;
   }

   if (count == N800_MCSPI_RESET_ITERATIONS)
      return -ETIMEDOUT;

   conf = readl(keypad->regs + MCSPI_MODULCTRL);
   conf &= ~(MCSPI_MODULCTRL_STEST | MCSPI_MODULCTRL_MS);
   conf |= MCSPI_MODULCTRL_SINGLE;
   writel(conf, keypad->regs + MCSPI_MODULCTRL);

   conf = readl(keypad->regs + MCSPI_CHCONF0);
   conf &= ~(MCSPI_CHCONF_PHA | MCSPI_CHCONF_POL | MCSPI_CHCONF_CLKD_MASK |
            MCSPI_CHCONF_WL_MASK | MCSPI_CHCONF_TRM_MASK | MCSPI_CHCONF_DPE1 |
            MCSPI_CHCONF_IS | MCSPI_CHCONF_FORCE);
   conf |= MCSPI_CHCONF_EPOL | MCSPI_CHCONF_DPE0;
   conf |= 3 << MCSPI_CHCONF_CLKD_SHIFT;
   conf |= (16 - 1) << MCSPI_CHCONF_WL_SHIFT;
   writel(conf, keypad->regs + MCSPI_CHCONF0);

   return 0;
}

static int n800_tsc2301_keypad_probe(struct device *dev)
{
   struct n800_tsc2301_keypad *keypad;
   u16 state;
   int ret;

   keypad = xzalloc(sizeof(*keypad));
   keypad->regs = IOMEM(N800_MCSPI1_BASE);
   keypad->input.parent = dev;

   ret = n800_mcspi_init(keypad);
   if (ret) {
      dev_err(dev, "McSPI reset timed out\n");
      return ret;
   }

   ret = n800_tsc2301_read_kpdata(keypad, &state);
   if (ret) {
      dev_err(dev, "failed to read TSC2301 KPDATA: %pe\n", ERR_PTR(ret));
      return ret;
   }

   keypad->previous = state;

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

   dev_info(dev, "TSC2301 keypad registered, KPDATA=0x%04x\n", state);

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
