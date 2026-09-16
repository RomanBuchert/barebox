// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <linux/sizes.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>

#define N800_SDRAM_BASE                    0x80000000
#define N800_SDRAM_SIZE                    SZ_128M

#define OMAP2420_CM_FCLKEN_WKUP            0x48008400
#define OMAP2420_CM_ICLKEN_WKUP            0x48008410
#define OMAP2420_GPIO_CLOCK_BIT            (1U << 2)

#define OMAP2420_CM_FCLKEN1_CORE           0x48008200
#define OMAP2420_CM_ICLKEN1_CORE           0x48008210
#define OMAP2420_CM_CLKSEL2_CORE           0x48008244
#define OMAP2420_GPT9_CLOCK_BIT            (1U << 11)
#define OMAP2420_GPT10_CLOCK_BIT           (1U << 12)
#define OMAP2420_GPT9_CLKSEL_SHIFT         16
#define OMAP2420_GPT10_CLKSEL_SHIFT        18
#define OMAP2420_GPT_CLKSEL_MASK           0x3U

#define OMAP2420_GPIO3_BASE                0x4801c000
#define OMAP24XX_GPIO_OE                   0x0034
#define OMAP24XX_GPIO_CLEARDATAOUT         0x0090
#define OMAP24XX_GPIO_SETDATAOUT           0x0094

#define N800_CBUS_SEL_BIT                  (1U << 0)
#define N800_CBUS_DAT_BIT                  (1U << 1)
#define N800_CBUS_CLK_BIT                  (1U << 2)
#define N800_GPT9_GPIO_BIT                 (1U << 3) /* GPIO67 */
#define N800_GPT10_GPIO_BIT                (1U << 4) /* GPIO68 */

#define OMAP2420_CONTROL_PADCONF_MUX_BASE  0x48000030
#define OMAP2420_PADCONF_UART2_CTS         (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0bb)
#define OMAP2420_PADCONF_UART2_RTS         (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0bc)
#define OMAP2420_MUX_MODE_GPIO             0x03
#define OMAP2420_MUX_MODE_GPT_PWM          0x02

#define RETU_CBUS_ADDRESS                  0x01
#define RETU_REG_CTRL_SET                  0x10

#define BETTY_CBUS_ADDRESS                 0x02
#define BETTY_REG_LED_PWM                  0x05
#define BETTY_BACKLIGHT_MAX                0x007fU

#define OMAP2420_GPT10_BASE                0x48086000

#define OMAP_TIMER_TIOCP_CFG               0x0010
#define OMAP_TIMER_TISTAT                  0x0014
#define OMAP_TIMER_TCLR                    0x0024
#define OMAP_TIMER_TCRR                    0x0028
#define OMAP_TIMER_TLDR                    0x002c
#define OMAP_TIMER_TTGR                    0x0030
#define OMAP_TIMER_TWPS                    0x0034
#define OMAP_TIMER_TMAR                    0x0038

#define OMAP_TIMER_SOFTRESET               (1U << 1)
#define OMAP_TIMER_RESETDONE               (1U << 0)

#define OMAP_TIMER_TCLR_ST                 (1U << 0)
#define OMAP_TIMER_TCLR_AR                 (1U << 1)
#define OMAP_TIMER_TCLR_CE                 (1U << 6)
#define OMAP_TIMER_TCLR_SCPWM              (1U << 7)
#define OMAP_TIMER_TCLR_TRG_OVF_MATCH      (2U << 10)
#define OMAP_TIMER_TCLR_PT                 (1U << 12)

#define PWM_LOAD                           0xffffff00U

static inline u32 mmio_read32(unsigned long address)
{
   return *(volatile u32 *)address;
}

static inline void mmio_write32(unsigned long address, u32 value)
{
   *(volatile u32 *)address = value;
}

static inline void mmio_write8(unsigned long address, u8 value)
{
   *(volatile u8 *)address = value;
}

static void short_delay(void)
{
   volatile unsigned int i;

   for (i = 0; i < 64; i++)
      __asm__ __volatile__("nop");
}

static void visible_delay(void)
{
   volatile unsigned int i;

   for (i = 0; i < 12000000; i++)
      __asm__ __volatile__("nop");
}

static void gpio3_set_output(u32 mask)
{
   u32 value = mmio_read32(OMAP2420_GPIO3_BASE + OMAP24XX_GPIO_OE);

   value &= ~mask;
   mmio_write32(OMAP2420_GPIO3_BASE + OMAP24XX_GPIO_OE, value);
}

static void gpio3_set(u32 mask)
{
   mmio_write32(OMAP2420_GPIO3_BASE + OMAP24XX_GPIO_SETDATAOUT, mask);
}

static void gpio3_clear(u32 mask)
{
   mmio_write32(OMAP2420_GPIO3_BASE + OMAP24XX_GPIO_CLEARDATAOUT, mask);
}

static void cbus_clock_bit(int bit)
{
   if (bit)
      gpio3_set(N800_CBUS_DAT_BIT);
   else
      gpio3_clear(N800_CBUS_DAT_BIT);

   short_delay();
   gpio3_set(N800_CBUS_CLK_BIT);
   short_delay();
   gpio3_clear(N800_CBUS_CLK_BIT);
   short_delay();
}

static void cbus_send_bits(u32 value, unsigned int count)
{
   int bit;

   for (bit = (int)count - 1; bit >= 0; bit--)
      cbus_clock_bit((value >> bit) & 1U);
}

static void cbus_write(u8 device, u8 reg, u16 value)
{
   gpio3_clear(N800_CBUS_SEL_BIT);
   short_delay();

   cbus_send_bits(device & 0x07, 3);
   cbus_clock_bit(0);
   cbus_send_bits(reg & 0x1f, 5);
   cbus_send_bits(value, 16);

   gpio3_set(N800_CBUS_SEL_BIT);
   short_delay();
   gpio3_set(N800_CBUS_CLK_BIT);
   short_delay();
   gpio3_clear(N800_CBUS_CLK_BIT);
   short_delay();
}

static void n800_cbus_init(void)
{
   u32 value;

   value = mmio_read32(OMAP2420_CM_ICLKEN_WKUP);
   mmio_write32(OMAP2420_CM_ICLKEN_WKUP, value | OMAP2420_GPIO_CLOCK_BIT);

   value = mmio_read32(OMAP2420_CM_FCLKEN_WKUP);
   mmio_write32(OMAP2420_CM_FCLKEN_WKUP, value | OMAP2420_GPIO_CLOCK_BIT);

   gpio3_set(N800_CBUS_SEL_BIT);
   gpio3_clear(N800_CBUS_CLK_BIT | N800_CBUS_DAT_BIT);
   gpio3_set_output(N800_CBUS_SEL_BIT | N800_CBUS_DAT_BIT | N800_CBUS_CLK_BIT);
}

static void n800_blue_led_supply_on(void)
{
   volatile unsigned int i;

   cbus_write(RETU_CBUS_ADDRESS, RETU_REG_CTRL_SET, 1U << 6);

   for (i = 0; i < 20000; i++)
      __asm__ __volatile__("nop");

   cbus_write(RETU_CBUS_ADDRESS, RETU_REG_CTRL_SET, 1U << 3);
}

static void timer_wait_write(unsigned long base)
{
   while (mmio_read32(base + OMAP_TIMER_TWPS))
      ;
}

static void timer_write(unsigned long base, u32 reg, u32 value)
{
   mmio_write32(base + reg, value);
   timer_wait_write(base);
}

static void timer_reset(unsigned long base)
{
   unsigned int timeout;

   mmio_write32(base + OMAP_TIMER_TIOCP_CFG, OMAP_TIMER_SOFTRESET);

   for (timeout = 0; timeout < 100000; timeout++) {
      if (mmio_read32(base + OMAP_TIMER_TISTAT) & OMAP_TIMER_RESETDONE)
         break;
   }
}

static void n800_gpt10_clock_init(void)
{
   u32 value;

   value = mmio_read32(OMAP2420_CM_CLKSEL2_CORE);
   value &= ~(OMAP2420_GPT_CLKSEL_MASK << OMAP2420_GPT10_CLKSEL_SHIFT);
   mmio_write32(OMAP2420_CM_CLKSEL2_CORE, value);

   value = mmio_read32(OMAP2420_CM_ICLKEN1_CORE);
   mmio_write32(OMAP2420_CM_ICLKEN1_CORE, value | OMAP2420_GPT10_CLOCK_BIT);

   value = mmio_read32(OMAP2420_CM_FCLKEN1_CORE);
   mmio_write32(OMAP2420_CM_FCLKEN1_CORE, value | OMAP2420_GPT10_CLOCK_BIT);
}

static void n800_gpt9_gate_gpio_high(void)
{
   /*
    * Keep the first D4802 AND input proven-high using GPIO67.
    * This intentionally removes GPT9 and its clock from this test.
    */
   mmio_write8(OMAP2420_PADCONF_UART2_CTS, OMAP2420_MUX_MODE_GPIO);
   gpio3_set(N800_GPT9_GPIO_BIT);
   gpio3_set_output(N800_GPT9_GPIO_BIT);
}

static void n800_gpt10_pad_init(void)
{
   mmio_write8(OMAP2420_PADCONF_UART2_RTS, OMAP2420_MUX_MODE_GPT_PWM);
}

static void n800_gpt10_pwm_init(void)
{
   u32 tclr;

   timer_reset(OMAP2420_GPT10_BASE);

   /*
    * Configure GPT10 as a conventional free-running PWM:
    *
    *   TLDR = 0xffffff00 -> 256 counter clocks per PWM period
    *   TMAR              -> compare value / duty cycle
    *
    * Configure and start the timer once.  Brightness changes below only
    * update TMAR; the timer itself keeps running continuously.
    */
   timer_write(OMAP2420_GPT10_BASE, OMAP_TIMER_TLDR, PWM_LOAD);
   timer_write(OMAP2420_GPT10_BASE, OMAP_TIMER_TTGR, 0);

   /* Start with the lowest safe compare value. */
   timer_write(OMAP2420_GPT10_BASE, OMAP_TIMER_TMAR, PWM_LOAD + 2U);

   tclr = OMAP_TIMER_TCLR_AR |
          OMAP_TIMER_TCLR_CE |
          OMAP_TIMER_TCLR_SCPWM |
          OMAP_TIMER_TCLR_TRG_OVF_MATCH |
          OMAP_TIMER_TCLR_PT |
          OMAP_TIMER_TCLR_ST;

   timer_write(OMAP2420_GPT10_BASE, OMAP_TIMER_TCLR, tclr);
}

static void n800_gpt10_set_brightness(unsigned int brightness)
{
   /*
    * TMAR is the OMAP GPT equivalent of the STM32 CCR value.
    * Do not touch TCLR, TLDR, TCRR or TTGR while changing brightness.
    *
    * Keep two timer clocks away from reload/overflow for this first test.
    */
   if (brightness < 2)
      brightness = 2;
   if (brightness > 253)
      brightness = 253;

   timer_write(OMAP2420_GPT10_BASE,
               OMAP_TIMER_TMAR,
               PWM_LOAD + brightness);
}

static void n800_backlight_set_brightness(unsigned int brightness)
{
   if (brightness > BETTY_BACKLIGHT_MAX)
      brightness = BETTY_BACKLIGHT_MAX;

   cbus_write(BETTY_CBUS_ADDRESS, BETTY_REG_LED_PWM, (u16)brightness);
}

static void fade_step_delay(void)
{
   volatile unsigned int i;

   for (i = 0; i < 30000; i++)
      __asm__ __volatile__("nop");
}

static void n800_leds_set_fade_step(unsigned int step)
{
   unsigned int blue_brightness;
   unsigned int backlight_brightness;

   if (step > 251)
      step = 251;

   /*
    * GPT10 has the safe compare range 2..253, i.e. 252 usable values.
    * Betty exposes 128 backlight values, 0..127.
    *
    * Both outputs are driven in opposite directions. The integer scaling
    * reaches both endpoints exactly:
    *
    *   step =   0: blue =   2, backlight = 127
    *   step = 251: blue = 253, backlight =   0
    */
   blue_brightness = 2 + step;
   backlight_brightness = BETTY_BACKLIGHT_MAX - ((step * BETTY_BACKLIGHT_MAX) / 251U);

   n800_gpt10_set_brightness(blue_brightness);
   n800_backlight_set_brightness(backlight_brightness);
}

static void n800_leds_fade(bool up)
{
   unsigned int step;

   if (up) {
      for (step = 0; step <= 251; step++) {
         n800_leds_set_fade_step(step);
         fade_step_delay();
      }
   } else {
      for (step = 251; step > 0; step--) {
         n800_leds_set_fade_step(step);
         fade_step_delay();
      }

      n800_leds_set_fade_step(0);
      fade_step_delay();
   }
}

ENTRY_FUNCTION_WITHSTACK(start_nokia_n800, N800_SDRAM_BASE + N800_SDRAM_SIZE, r0, r1, r2)
{
   arm_cpu_lowlevel_init();

   n800_cbus_init();
   n800_blue_led_supply_on();

   n800_gpt9_gate_gpio_high();
   n800_gpt10_clock_init();
   n800_gpt10_pad_init();
   n800_gpt10_pwm_init();

   n800_leds_set_fade_step(0);

   for (;;) {
      n800_leds_fade(true);
      n800_leds_fade(false);
   }
}
