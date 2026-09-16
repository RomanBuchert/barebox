// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nokia N800 TUSB6010 peripheral glue for Barebox.
 *
 * The hardware sequence is based on Nokia's original N800/Maemo TUSB6010
 * support and the upstream Linux TUSB6010 MUSB driver. This implementation
 * intentionally starts with asynchronous GPMC PIO only: no DMA, no CS4
 * synchronous window, and no host/OTG role switching.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <device.h>
#include <clock.h>
#include <linux/usb/musb.h>
#include <linux/usb/phy.h>
#include <linux/usb/usbserial.h>
#include <mach/omap/gpmc.h>

#include "musb_core.h"
#include "tusb6010.h"

#define OMAP2420_GPMC_BASE                  0x6800a000
#define OMAP2420_GPIO1_BASE                 0x48018000
#define OMAP24XX_GPIO_OE                    0x0034
#define OMAP24XX_GPIO_SETDATAOUT            0x0094
#define OMAP24XX_GPIO_CLEARDATAOUT          0x0090

#define N800_TUSB_ENABLE                    (1U << 0)
#define N800_TUSB_ASYNC_CS                  1
#define N800_TUSB_ASYNC_BASE                0x10000000

#define GPMC_CS0_BASE                       0x60
#define GPMC_CS_STRIDE                      0x30
#define GPMC_CS_CONFIG1                     0x00
#define GPMC_CS_CONFIG2                     0x04
#define GPMC_CS_CONFIG3                     0x08
#define GPMC_CS_CONFIG4                     0x0c
#define GPMC_CS_CONFIG5                     0x10
#define GPMC_CS_CONFIG6                     0x14
#define GPMC_CS_CONFIG7                     0x18

/*
 * CONFIG1 is the original Nokia asynchronous TUSB6010 bus mode:
 * 16-bit multiplexed NOR-like bus, WAIT2 on reads and writes.
 */
#define N800_TUSB_GPMC_CONFIG1              0x01621200

/*
 * These timing registers are the original Nokia asynchronous timing
 * equations evaluated conservatively for a 166.7 MHz maximum GPMC fclk.
 * Any slower GPMC clock lengthens the external bus cycles. nRDY/WAIT2
 * remains enabled and can extend accesses further.
 */
#define N800_TUSB_GPMC_CONFIG2              0x000c0c02
#define N800_TUSB_GPMC_CONFIG3              0x000a0a08
#define N800_TUSB_GPMC_CONFIG4              0x0c0b0c0a
#define N800_TUSB_GPMC_CONFIG5              0x000b0e0e
#define N800_TUSB_GPMC_CONFIG6              0x00000000
#define N800_TUSB_GPMC_CONFIG7              0x00000f50

static struct musb n800_musb;

static struct usb_phy n800_tusb_phy = {
   .label = "n800-tusb6010",
   .type = USB_PHY_TYPE_USB2,
};

static struct device n800_tusb_device = {
   .name = "n800-tusb6010",
   .id = DEVICE_ID_SINGLE,
};

static int n800_tusb_isr(struct musb *musb);

static struct musb_hdrc_config n800_tusb_config = {
   .multipoint = 1,
   .dyn_fifo = 1,
   .num_eps = 16,
   .ram_bits = 12,
};

static void __iomem *n800_gpmc_cs_reg(unsigned int cs, unsigned int reg)
{
   return omap_gpmc_base + GPMC_CS0_BASE + cs * GPMC_CS_STRIDE + reg;
}

static void n800_tusb_power(bool on)
{
   void __iomem *gpio = (void __iomem *)OMAP2420_GPIO1_BASE;
   u32 oe;

   oe = readl(gpio + OMAP24XX_GPIO_OE);
   oe &= ~N800_TUSB_ENABLE;
   writel(oe, gpio + OMAP24XX_GPIO_OE);

   if (on)
      writel(N800_TUSB_ENABLE, gpio + OMAP24XX_GPIO_SETDATAOUT);
   else
      writel(N800_TUSB_ENABLE, gpio + OMAP24XX_GPIO_CLEARDATAOUT);
}

static void n800_tusb_configure_gpmc(void)
{
   unsigned int cs = N800_TUSB_ASYNC_CS;

   omap_gpmc_base = (void __iomem *)OMAP2420_GPMC_BASE;

   writel(0, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG7));
   writel(N800_TUSB_GPMC_CONFIG1, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG1));
   writel(N800_TUSB_GPMC_CONFIG2, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG2));
   writel(N800_TUSB_GPMC_CONFIG3, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG3));
   writel(N800_TUSB_GPMC_CONFIG4, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG4));
   writel(N800_TUSB_GPMC_CONFIG5, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG5));
   writel(N800_TUSB_GPMC_CONFIG6, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG6));
   writel(N800_TUSB_GPMC_CONFIG7, n800_gpmc_cs_reg(cs, GPMC_CS_CONFIG7));
}

static void n800_tusb_set_clock_source(struct musb *musb, unsigned int mode)
{
   void __iomem *tbase = musb->ctrl_base;
   u32 reg;

   reg = musb_readl(tbase, TUSB_PRCM_CONF);
   reg &= ~TUSB_PRCM_CONF_SYS_CLKSEL(3);
   reg |= TUSB_PRCM_CONF_SYS_CLKSEL(mode);
   musb_writel(tbase, TUSB_PRCM_CONF, reg);
}

static void n800_tusb_setup_cpu_interface(struct musb *musb)
{
   void __iomem *tbase = musb->ctrl_base;

   musb_writel(tbase, TUSB_PULLUP_1_CTRL, 0x0000003f);
   musb_writel(tbase, TUSB_PULLUP_2_CTRL, 0x01ffffff);
   musb_writel(tbase, TUSB_GPIO_CONF, TUSB_GPIO_CONF_DMAREQ(0x3f));
   musb_writel(tbase, TUSB_DMA_REQ_CONF,
               TUSB_DMA_REQ_CONF_BURST_SIZE(2) |
               TUSB_DMA_REQ_CONF_DMA_REQ_EN(0x3f) |
               TUSB_DMA_REQ_CONF_DMA_REQ_ASSER(2));
   musb_writel(tbase, TUSB_WAIT_COUNT, 1);
}

static int n800_tusb_set_mode(struct musb *musb, u8 mode)
{
   void __iomem *tbase = musb->ctrl_base;
   u32 dev_conf;
   u32 phy_ctrl;
   u32 phy_enable;

   if (mode != MUSB_PERIPHERAL)
      return -EINVAL;

   dev_conf = musb_readl(tbase, TUSB_DEV_CONF);
   dev_conf |= TUSB_DEV_CONF_ID_SEL | TUSB_DEV_CONF_SOFT_ID;

   phy_ctrl = musb_readl(tbase, TUSB_PHY_OTG_CTRL);
   phy_enable = musb_readl(tbase, TUSB_PHY_OTG_CTRL_ENABLE);

   phy_ctrl |= TUSB_PHY_OTG_CTRL_WRPROTECT | TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP |
               TUSB_PHY_OTG_CTRL_OTG_VBUS_DET_EN |
               TUSB_PHY_OTG_CTRL_OTG_SESS_END_EN;
   phy_enable |= TUSB_PHY_OTG_CTRL_WRPROTECT | TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP |
                 TUSB_PHY_OTG_CTRL_OTG_VBUS_DET_EN |
                 TUSB_PHY_OTG_CTRL_OTG_SESS_END_EN;

   musb_writel(tbase, TUSB_PHY_OTG_CTRL, phy_ctrl);
   musb_writel(tbase, TUSB_PHY_OTG_CTRL_ENABLE, phy_enable);
   musb_writel(tbase, TUSB_DEV_CONF, dev_conf);

   return 0;
}

static int n800_tusb_phy_set_power(struct usb_phy *phy, unsigned int mA)
{
   (void)phy;
   (void)mA;
   return 0;
}

static int n800_tusb_platform_init(struct musb *musb)
{
   void __iomem *tbase = musb->ctrl_base;
   u32 reg;
   u32 test;

   n800_tusb_phy.set_power = n800_tusb_phy_set_power;
   musb->xceiv = &n800_tusb_phy;

   n800_tusb_power(true);

   /*
    * Real hardware normally becomes ready well below this delay. QEMU's
    * N800 TUSB6010 model deliberately completes power-up after 500 ms.
    */
   mdelay(600);

   test = musb_readl(tbase, TUSB_PROD_TEST_RESET);
   pr_info("n800-usb: TUSB6010 PROD_TEST_RESET = 0x%08x\n", test);
   if ((test & 0xffff) != TUSB_PROD_TEST_RESET_VAL) {
      pr_err("n800-usb: TUSB6010 not detected\n");
      n800_tusb_power(false);
      return -ENODEV;
   }

   musb_writel(tbase, TUSB_VLYNQ_CTRL, 8);
   n800_tusb_set_clock_source(musb, 1);

   musb_writel(tbase, TUSB_PRCM_MNGMT,
               TUSB_PRCM_MNGMT_VBUS_VALID_TIMER(0xa) |
               TUSB_PRCM_MNGMT_VBUS_VALID_FLT_EN |
               TUSB_PRCM_MNGMT_OTG_SESS_END_EN |
               TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN |
               TUSB_PRCM_MNGMT_OTG_ID_PULLUP);

   n800_tusb_setup_cpu_interface(musb);

   reg = musb_readl(tbase, TUSB_PHY_OTG_CTRL_ENABLE);
   reg |= TUSB_PHY_OTG_CTRL_WRPROTECT | TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP;
   musb_writel(tbase, TUSB_PHY_OTG_CTRL_ENABLE, reg);

   reg = musb_readl(tbase, TUSB_PHY_OTG_CTRL);
   reg |= TUSB_PHY_OTG_CTRL_WRPROTECT | TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP;
   musb_writel(tbase, TUSB_PHY_OTG_CTRL, reg);

   musb->mregs += TUSB_BASE_OFFSET;
   musb->isr = n800_tusb_isr;

   n800_tusb_set_mode(musb, MUSB_PERIPHERAL);

   pr_info("n800-usb: TUSB6010 peripheral/PIO initialized\n");

   return 0;
}

static int n800_tusb_platform_exit(struct musb *musb)
{
   (void)musb;
   n800_tusb_power(false);
   mdelay(10);
   return 0;
}

static void n800_tusb_platform_enable(struct musb *musb)
{
   void __iomem *tbase = musb->ctrl_base;

   musb_writel(tbase, TUSB_INT_MASK, TUSB_INT_SRC_USB_IP_SOF);
   musb_writel(tbase, TUSB_USBIP_INT_MASK, 0);
   musb_writel(tbase, TUSB_DMA_INT_MASK, 0x7fffffff);
   musb_writel(tbase, TUSB_GPIO_INT_MASK, 0x1ff);

   musb_writel(tbase, TUSB_USBIP_INT_CLEAR, 0x7fffffff);
   musb_writel(tbase, TUSB_DMA_INT_CLEAR, 0x7fffffff);
   musb_writel(tbase, TUSB_GPIO_INT_CLEAR, 0x1ff);
   musb_writel(tbase, TUSB_INT_SRC_CLEAR, ~TUSB_INT_MASK_RESERVED_BITS);

   musb_writel(tbase, TUSB_INT_CTRL_CONF, TUSB_INT_CTRL_CONF_INT_RELCYC(0));
}

static void n800_tusb_platform_disable(struct musb *musb)
{
   void __iomem *tbase = musb->ctrl_base;

   musb_writel(tbase, TUSB_INT_MASK, ~TUSB_INT_MASK_RESERVED_BITS);
   musb_writel(tbase, TUSB_USBIP_INT_MASK, 0x7fffffff);
   musb_writel(tbase, TUSB_DMA_INT_MASK, 0x7fffffff);
   musb_writel(tbase, TUSB_GPIO_INT_MASK, 0x1ff);
}

static int n800_tusb_isr(struct musb *musb)
{
   void __iomem *tbase = musb->ctrl_base;
   u32 int_src;
   u32 musb_src;
   int handled = 0;

   int_src = musb_readl(tbase, TUSB_INT_SRC) & ~TUSB_INT_SRC_RESERVED_BITS;

   musb->int_usb = int_src & 0xff;

   if (int_src & (TUSB_INT_SRC_USB_IP_TX | TUSB_INT_SRC_USB_IP_RX)) {
      musb_src = musb_readl(tbase, TUSB_USBIP_INT_SRC);
      musb_writel(tbase, TUSB_USBIP_INT_CLEAR, musb_src);

      musb->int_rx = (((musb_src >> 16) & 0xffff) << 1);
      musb->int_tx = musb_src & 0xffff;
   } else {
      musb->int_rx = 0;
      musb->int_tx = 0;
   }

   if (int_src & (TUSB_INT_SRC_USB_IP_TX | TUSB_INT_SRC_USB_IP_RX | 0xff)) {
      handled = musb_interrupt(musb);
   }

   if (int_src)
      musb_writel(tbase, TUSB_INT_SRC_CLEAR,
                  int_src & ~TUSB_INT_MASK_RESERVED_BITS);

   return handled;
}

static const struct musb_platform_ops n800_tusb_ops = {
   .init = n800_tusb_platform_init,
   .exit = n800_tusb_platform_exit,
   .enable = n800_tusb_platform_enable,
   .disable = n800_tusb_platform_disable,
   .set_mode = n800_tusb_set_mode,
};

void musb_write_fifo(struct musb_hw_ep *hw_ep, u16 len, const u8 *buf)
{
   void __iomem *ep_conf = hw_ep->conf;
   void __iomem *fifo = hw_ep->fifo;
   u32 val;

   if (!len)
      return;

   if (hw_ep->epnum)
      musb_writel(ep_conf, TUSB_EP_TX_OFFSET, TUSB_EP_CONFIG_XFR_SIZE(len));
   else
      musb_writel(ep_conf, 0,
                  TUSB_EP0_CONFIG_DIR_TX | TUSB_EP0_CONFIG_XFR_SIZE(len));

   while (len >= 4) {
      memcpy(&val, buf, sizeof(val));
      musb_writel(fifo, 0, val);
      buf += 4;
      len -= 4;
   }

   if (len) {
      val = 0;
      memcpy(&val, buf, len);
      musb_writel(fifo, 0, val);
   }
}

void musb_read_fifo(struct musb_hw_ep *hw_ep, u16 len, u8 *buf)
{
   void __iomem *ep_conf = hw_ep->conf;
   void __iomem *fifo = hw_ep->fifo;
   u32 val;

   if (!len)
      return;

   if (hw_ep->epnum)
      musb_writel(ep_conf, TUSB_EP_RX_OFFSET, TUSB_EP_CONFIG_XFR_SIZE(len));
   else
      musb_writel(ep_conf, 0, TUSB_EP0_CONFIG_XFR_SIZE(len));

   while (len >= 4) {
      val = musb_readl(fifo, 0);
      memcpy(buf, &val, sizeof(val));
      buf += 4;
      len -= 4;
   }

   if (len) {
      val = musb_readl(fifo, 0);
      memcpy(buf, &val, len);
   }
}

static int n800_tusb_init(void)
{
   struct musb_hdrc_platform_data pdata = {
      .mode = MUSB_PORT_MODE_GADGET,
      .min_power = 50,
      .config = &n800_tusb_config,
      .platform_ops = &n800_tusb_ops,
   };
   int ret;

   pr_info("n800-usb: initializing TUSB6010 MUSB peripheral\n");

   n800_tusb_configure_gpmc();

   ret = register_device(&n800_tusb_device);
   if (ret) {
      pr_err("n800-usb: failed to register controller device: %pe\n",
             ERR_PTR(ret));
      return ret;
   }

   n800_musb.controller = &n800_tusb_device;
   n800_musb.ctrl_base = (void __iomem *)N800_TUSB_ASYNC_BASE;
   n800_musb.mregs = (void __iomem *)N800_TUSB_ASYNC_BASE;

   ret = musb_init_controller(&n800_musb, &pdata);
   if (ret) {
      pr_err("n800-usb: MUSB controller initialization failed: %pe\n",
             ERR_PTR(ret));
      return ret;
   }

   pr_info("n800-usb: MUSB gadget controller registered\n");

   return 0;
}
device_initcall(n800_tusb_init);

static int n800_tusb_cdc_init(void)
{
   struct usb_serial_pdata serial_pdata = {
      .acm = 1,
   };
   int ret;

   pr_info("n800-usb: starting CDC ACM gadget\n");

   ret = usb_serial_register(&serial_pdata);
   if (ret) {
      pr_err("n800-usb: CDC ACM gadget registration failed: %pe\n",
             ERR_PTR(ret));
      return ret;
   }

   pr_info("n800-usb: CDC ACM gadget registered\n");

   return 0;
}
late_initcall(n800_tusb_cdc_init);
