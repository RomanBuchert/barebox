/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal TUSB6010 definitions used by the Barebox MUSB core and
 * Nokia N800 glue.
 */

#ifndef __BAREBOX_MUSB_TUSB6010_H__
#define __BAREBOX_MUSB_TUSB6010_H__

#define TUSB_VLYNQ_CTRL                     0x004
#define TUSB_BASE_OFFSET                    0x400
#define TUSB_FIFO_BASE                      0x600
#define TUSB_SYS_REG_BASE                   0x800

#define TUSB_DEV_CONF                       (TUSB_SYS_REG_BASE + 0x000)
#define TUSB_DEV_CONF_SOFT_ID               (1U << 1)
#define TUSB_DEV_CONF_ID_SEL                (1U << 0)

#define TUSB_PHY_OTG_CTRL_ENABLE            (TUSB_SYS_REG_BASE + 0x004)
#define TUSB_PHY_OTG_CTRL                   (TUSB_SYS_REG_BASE + 0x008)
#define TUSB_PHY_OTG_CTRL_WRPROTECT         (0xa5U << 24)
#define TUSB_PHY_OTG_CTRL_OTG_ID_PULLUP     (1U << 23)
#define TUSB_PHY_OTG_CTRL_OTG_VBUS_DET_EN   (1U << 19)
#define TUSB_PHY_OTG_CTRL_OTG_SESS_END_EN   (1U << 18)

#define TUSB_DEV_OTG_STAT                   (TUSB_SYS_REG_BASE + 0x00c)
#define TUSB_DEV_OTG_STAT_VBUS_VALID        (1U << 5)

#define TUSB_PRCM_CONF                      (TUSB_SYS_REG_BASE + 0x018)
#define TUSB_PRCM_CONF_SYS_CLKSEL(v)        (((v) & 3U) << 16)

#define TUSB_PRCM_MNGMT                     (TUSB_SYS_REG_BASE + 0x01c)
#define TUSB_PRCM_MNGMT_VBUS_VALID_TIMER(v) (((v) & 0xfU) << 20)
#define TUSB_PRCM_MNGMT_VBUS_VALID_FLT_EN   (1U << 19)
#define TUSB_PRCM_MNGMT_OTG_SESS_END_EN     (1U << 10)
#define TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN     (1U << 9)
#define TUSB_PRCM_MNGMT_OTG_ID_PULLUP       (1U << 8)

#define TUSB_PULLUP_1_CTRL                  (TUSB_SYS_REG_BASE + 0x030)
#define TUSB_PULLUP_2_CTRL                  (TUSB_SYS_REG_BASE + 0x034)
#define TUSB_INT_CTRL_CONF                  (TUSB_SYS_REG_BASE + 0x03c)
#define TUSB_USBIP_INT_SRC                  (TUSB_SYS_REG_BASE + 0x040)
#define TUSB_USBIP_INT_CLEAR                (TUSB_SYS_REG_BASE + 0x048)
#define TUSB_USBIP_INT_MASK                 (TUSB_SYS_REG_BASE + 0x04c)
#define TUSB_DMA_INT_CLEAR                  (TUSB_SYS_REG_BASE + 0x058)
#define TUSB_DMA_INT_MASK                   (TUSB_SYS_REG_BASE + 0x05c)
#define TUSB_GPIO_INT_CLEAR                 (TUSB_SYS_REG_BASE + 0x068)
#define TUSB_GPIO_INT_MASK                  (TUSB_SYS_REG_BASE + 0x06c)

#define TUSB_INT_SRC                        (TUSB_SYS_REG_BASE + 0x070)
#define TUSB_INT_SRC_CLEAR                  (TUSB_SYS_REG_BASE + 0x078)
#define TUSB_INT_MASK                       (TUSB_SYS_REG_BASE + 0x07c)
#define TUSB_INT_SRC_USB_IP_TX              (1U << 9)
#define TUSB_INT_SRC_USB_IP_RX              (1U << 8)
#define TUSB_INT_SRC_USB_IP_VBUS_ERR        (1U << 7)
#define TUSB_INT_SRC_USB_IP_VBUS_REQ        (1U << 6)
#define TUSB_INT_SRC_USB_IP_DISCON          (1U << 5)
#define TUSB_INT_SRC_USB_IP_CONN            (1U << 4)
#define TUSB_INT_SRC_USB_IP_SOF             (1U << 3)
#define TUSB_INT_SRC_USB_IP_RST_BABBLE      (1U << 2)
#define TUSB_INT_SRC_USB_IP_RESUME           (1U << 1)
#define TUSB_INT_SRC_USB_IP_SUSPEND          (1U << 0)

#define TUSB_INT_MASK_RESERVED_17           (0x3fffU << 17)
#define TUSB_INT_MASK_RESERVED_13           (1U << 13)
#define TUSB_INT_MASK_RESERVED_8            (0xfU << 8)
#define TUSB_INT_SRC_RESERVED_26            (0x1fU << 26)
#define TUSB_INT_SRC_RESERVED_18            (0x3fU << 18)
#define TUSB_INT_SRC_RESERVED_10            (0x03U << 10)

#define TUSB_INT_MASK_RESERVED_BITS         \
   (TUSB_INT_MASK_RESERVED_17 | TUSB_INT_MASK_RESERVED_13 | \
    TUSB_INT_MASK_RESERVED_8)

#define TUSB_INT_SRC_RESERVED_BITS          \
   (TUSB_INT_SRC_RESERVED_26 | TUSB_INT_SRC_RESERVED_18 | \
    TUSB_INT_SRC_RESERVED_10)

#define TUSB_GPIO_CONF                      (TUSB_SYS_REG_BASE + 0x084)
#define TUSB_DMA_REQ_CONF                   (TUSB_SYS_REG_BASE + 0x104)
#define TUSB_EP0_CONF                       (TUSB_SYS_REG_BASE + 0x108)

#define TUSB_EP_TX_OFFSET                   0x10c
#define TUSB_EP_RX_OFFSET                   0x14c
#define TUSB_WAIT_COUNT                     (TUSB_SYS_REG_BASE + 0x1c8)
#define TUSB_PROD_TEST_RESET                (TUSB_SYS_REG_BASE + 0x1d8)

#define TUSB_INT_CTRL_CONF_INT_RELCYC(v)    (((v) & 0x7U) << 18)
#define TUSB_GPIO_CONF_DMAREQ(v)            (((v) & 0x3fU) << 24)
#define TUSB_DMA_REQ_CONF_BURST_SIZE(v)     (((v) & 3U) << 26)
#define TUSB_DMA_REQ_CONF_DMA_REQ_EN(v)     (((v) & 0x3fU) << 20)
#define TUSB_DMA_REQ_CONF_DMA_REQ_ASSER(v)  (((v) & 0xfU) << 16)

#define TUSB_EP0_CONFIG_DIR_TX              (1U << 7)
#define TUSB_EP0_CONFIG_XFR_SIZE(v)         ((v) & 0x7fU)
#define TUSB_EP_CONFIG_XFR_SIZE(v)          ((v) & 0x7fffffffU)

#define TUSB_PROD_TEST_RESET_VAL            0xa596

#endif
