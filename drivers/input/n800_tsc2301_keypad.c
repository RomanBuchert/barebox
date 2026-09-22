// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <driver.h>
#include <errno.h>
#include <init.h>
#include <input/input.h>
#include <io.h>
#include <poller.h>

#define N800_MCSPI1_BASE                  0x48098000

#define OMAP2420_CM_FCLKEN1_CORE          0x48008200
#define OMAP2420_CM_ICLKEN1_CORE          0x48008210
#define OMAP2420_MCSPI1_CLOCK_BIT         BIT(17)

#define OMAP2420_CONTROL_PADCONF_MUX_BASE 0x48000030
#define OMAP2420_PADCONF_SPI1_CLK         (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0cf)
#define OMAP2420_PADCONF_SPI1_SIMO        (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d0)
#define OMAP2420_PADCONF_SPI1_SOMI        (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d1)
#define OMAP2420_PADCONF_SPI1_NCS0        (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d2)
#define OMAP2420_MUX_MODE0                0x00

#define MCSPI_SYSCONFIG                   0x10
#define MCSPI_SYSSTATUS                   0x14
#define MCSPI_MODULCTRL                   0x28
#define MCSPI_CHCONF0                     0x2c
#define MCSPI_CHSTAT0                     0x30
#define MCSPI_CHCTRL0                     0x34
#define MCSPI_TX0                         0x38
#define MCSPI_RX0                         0x3c

#define MCSPI_SYSCONFIG_SOFTRESET         BIT(1)
#define MCSPI_SYSSTATUS_RESETDONE         BIT(0)
#define MCSPI_MODULCTRL_SINGLE            BIT(0)
#define MCSPI_MODULCTRL_MS                BIT(2)
#define MCSPI_MODULCTRL_STEST             BIT(3)

#define MCSPI_CHCONF_PHA                  BIT(0)
#define MCSPI_CHCONF_POL                  BIT(1)
#define MCSPI_CHCONF_CLKD_SHIFT           2
#define MCSPI_CHCONF_CLKD_MASK            (0xf << MCSPI_CHCONF_CLKD_SHIFT)
#define MCSPI_CHCONF_EPOL                 BIT(6)
#define MCSPI_CHCONF_WL_SHIFT             7
#define MCSPI_CHCONF_WL_MASK              (0x1f << MCSPI_CHCONF_WL_SHIFT)
#define MCSPI_CHCONF_TRM_MASK             (0x3 << 12)
#define MCSPI_CHCONF_DPE0                 BIT(16)
#define MCSPI_CHCONF_DPE1                 BIT(17)
#define MCSPI_CHCONF_IS                   BIT(18)
#define MCSPI_CHCONF_FORCE                BIT(20)

#define MCSPI_CHSTAT_RXS                  BIT(0)
#define MCSPI_CHSTAT_TXS                  BIT(1)
#define MCSPI_CHCTRL_EN                   BIT(0)

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

static void n800_mmio_write8(unsigned long address, u8 value)
{
   *(volatile u8 *)address = value;
}

static void n800_mcspi_hw_enable(void)
{
   u32 value;

   value = readl(IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   writel(value | OMAP2420_MCSPI1_CLOCK_BIT, IOMEM(OMAP2420_CM_ICLKEN1_CORE));

   value = readl(IOMEM(OMAP2420_CM_FCLKEN1_CORE));
   writel(value | OMAP2420_MCSPI1_CLOCK_BIT, IOMEM(OMAP2420_CM_FCLKEN1_CORE));

   /*
    * OMAP2420 padconf registers are 8-bit wide. SPI1 is the mode-0
    * function on these four pads.
    */
   n800_mmio_write8(OMAP2420_PADCONF_SPI1_CLK, OMAP2420_MUX_MODE0);
   n800_mmio_write8(OMAP2420_PADCONF_SPI1_SIMO, OMAP2420_MUX_MODE0);
   n800_mmio_write8(OMAP2420_PADCONF_SPI1_SOMI, OMAP2420_MUX_MODE0);
   n800_mmio_write8(OMAP2420_PADCONF_SPI1_NCS0, OMAP2420_MUX_MODE0);
}

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

static int n800_tsc2301_transfer(struct n800_tsc2301_keypad *keypad, u16 command,
                                 u16 tx, u16 *rx)
{
   u16 dummy;
   u32 conf;
   int ret;

   conf = readl(keypad->regs + MCSPI_CHCONF0);
   conf |= MCSPI_CHCONF_FORCE;
   writel(conf, keypad->regs + MCSPI_CHCONF0);
   writel(MCSPI_CHCTRL_EN, keypad->regs + MCSPI_CHCTRL0);

   ret = n800_mcspi_word(keypad, command, &dummy);
   if (!ret)
      ret = n800_mcspi_word(keypad, tx, rx);

   conf &= ~MCSPI_CHCONF_FORCE;
   writel(conf, keypad->regs + MCSPI_CHCONF0);
   writel(0, keypad->regs + MCSPI_CHCTRL0);

   return ret;
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

static int n800_mcspi_init(struct n800_tsc2301_keypad *keypad)
{
   unsigned int count;
   u32 conf;

   n800_mcspi_hw_enable();

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
