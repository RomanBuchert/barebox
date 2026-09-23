// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <errno.h>
#include <io.h>
#include <mach/omap/omap2-mcspi.h>

#define OMAP2_MCSPI1_BASE                  0x48098000
#define OMAP2_MCSPI_MAX_FREQ               48000000

#define OMAP2420_CM_FCLKEN1_CORE            0x48008200
#define OMAP2420_CM_ICLKEN1_CORE            0x48008210
#define OMAP2420_MCSPI1_CLOCK_BIT           BIT(17)

#define OMAP2420_CONTROL_PADCONF_MUX_BASE   0x48000030
#define OMAP2420_PADCONF_SPI1_CLK           (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0cf)
#define OMAP2420_PADCONF_SPI1_SIMO          (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d0)
#define OMAP2420_PADCONF_SPI1_SOMI          (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d1)
#define OMAP2420_PADCONF_SPI1_NCS0          (OMAP2420_CONTROL_PADCONF_MUX_BASE + 0x0d2)
#define OMAP2420_MUX_MODE0                  0x00

#define MCSPI_MODULCTRL                     0x28
#define MCSPI_CHCONF0                       0x2c
#define MCSPI_CHSTAT0                       0x30
#define MCSPI_CHCTRL0                       0x34
#define MCSPI_TX0                           0x38
#define MCSPI_RX0                           0x3c
#define MCSPI_CHANNEL_STRIDE                0x14

#define MCSPI_MODULCTRL_SINGLE              BIT(0)
#define MCSPI_MODULCTRL_MS                  BIT(2)
#define MCSPI_MODULCTRL_STEST               BIT(3)

#define MCSPI_CHCONF_PHA                    BIT(0)
#define MCSPI_CHCONF_POL                    BIT(1)
#define MCSPI_CHCONF_CLKD_SHIFT             2
#define MCSPI_CHCONF_CLKD_MASK              (0xf << MCSPI_CHCONF_CLKD_SHIFT)
#define MCSPI_CHCONF_EPOL                   BIT(6)
#define MCSPI_CHCONF_WL_SHIFT               7
#define MCSPI_CHCONF_WL_MASK                (0x1f << MCSPI_CHCONF_WL_SHIFT)
#define MCSPI_CHCONF_TRM_MASK               (0x3 << 12)
#define MCSPI_CHCONF_DPE0                   BIT(16)
#define MCSPI_CHCONF_DPE1                   BIT(17)
#define MCSPI_CHCONF_IS                     BIT(18)
#define MCSPI_CHCONF_FORCE                  BIT(20)

#define MCSPI_CHSTAT_RXS                    BIT(0)
#define MCSPI_CHSTAT_TXS                    BIT(1)
#define MCSPI_CHCTRL_EN                     BIT(0)

#define OMAP2_MCSPI_WAIT_ITERATIONS         4096

static void __iomem *const mcspi1 = IOMEM(OMAP2_MCSPI1_BASE);

static void omap2_mcspi_write8(unsigned long address, u8 value)
{
   *(volatile u8 *)address = value;
}

static void __iomem *omap2_mcspi_channel_reg(unsigned int channel, unsigned int reg)
{
   return mcspi1 + reg + channel * MCSPI_CHANNEL_STRIDE;
}

static int omap2_mcspi_wait(unsigned int channel, u32 mask)
{
   unsigned int count;

   for (count = 0; count < OMAP2_MCSPI_WAIT_ITERATIONS; count++) {
      if (readl(omap2_mcspi_channel_reg(channel, MCSPI_CHSTAT0)) & mask)
         return 0;
   }

   return -ETIMEDOUT;
}

static int omap2_mcspi_clock_divider(unsigned int max_speed_hz)
{
   unsigned int divider = 0;

   if (!max_speed_hz)
      return 15;

   while (divider <= 15 && (OMAP2_MCSPI_MAX_FREQ >> divider) > max_speed_hz)
      divider++;

   if (divider > 15)
      return -EINVAL;

   return divider;
}

static int omap2_mcspi_configure(const struct omap2_mcspi_device *device)
{
   unsigned int channel = device->chip_select;
   int divider;
   u32 conf;

   if (channel > 3 || device->bits_per_word < 4 || device->bits_per_word > 32)
      return -EINVAL;

   if (device->mode > OMAP2_MCSPI_MODE_3)
      return -EINVAL;

   divider = omap2_mcspi_clock_divider(device->max_speed_hz);
   if (divider < 0)
      return divider;

   conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   conf &= ~(MCSPI_CHCONF_PHA | MCSPI_CHCONF_POL | MCSPI_CHCONF_CLKD_MASK |
             MCSPI_CHCONF_WL_MASK | MCSPI_CHCONF_TRM_MASK | MCSPI_CHCONF_DPE1 |
             MCSPI_CHCONF_IS | MCSPI_CHCONF_FORCE);
   conf |= MCSPI_CHCONF_EPOL | MCSPI_CHCONF_DPE0;
   conf |= divider << MCSPI_CHCONF_CLKD_SHIFT;
   conf |= (device->bits_per_word - 1) << MCSPI_CHCONF_WL_SHIFT;

   if (device->mode & BIT(1))
      conf |= MCSPI_CHCONF_POL;

   /* OMAP2 PHA has the opposite sense of the Linux SPI_CPHA flag. */
   if (!(device->mode & BIT(0)))
      conf |= MCSPI_CHCONF_PHA;

   writel(conf, omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));

   return 0;
}

int omap2_mcspi1_setup(void)
{
   u32 value;

   value = readl(IOMEM(OMAP2420_CM_ICLKEN1_CORE));
   writel(value | OMAP2420_MCSPI1_CLOCK_BIT, IOMEM(OMAP2420_CM_ICLKEN1_CORE));

   value = readl(IOMEM(OMAP2420_CM_FCLKEN1_CORE));
   writel(value | OMAP2420_MCSPI1_CLOCK_BIT, IOMEM(OMAP2420_CM_FCLKEN1_CORE));

   omap2_mcspi_write8(OMAP2420_PADCONF_SPI1_CLK, OMAP2420_MUX_MODE0);
   omap2_mcspi_write8(OMAP2420_PADCONF_SPI1_SIMO, OMAP2420_MUX_MODE0);
   omap2_mcspi_write8(OMAP2420_PADCONF_SPI1_SOMI, OMAP2420_MUX_MODE0);
   omap2_mcspi_write8(OMAP2420_PADCONF_SPI1_NCS0, OMAP2420_MUX_MODE0);

   value = readl(mcspi1 + MCSPI_MODULCTRL);
   value &= ~(MCSPI_MODULCTRL_STEST | MCSPI_MODULCTRL_MS);
   value |= MCSPI_MODULCTRL_SINGLE;
   writel(value, mcspi1 + MCSPI_MODULCTRL);

   /*
    * Deliberately do not reset McSPI1 here. The N800 shares this controller
    * between TSC2301 on CS0 and the LCD panel on CS1, and a controller-wide
    * reset would destroy state belonging to the other slave.
    */
   return 0;
}

int omap2_mcspi1_transfer(const struct omap2_mcspi_device *device, const u32 *tx,
                          u32 *rx, size_t words)
{
   unsigned int channel;
   size_t word;
   u32 conf;
   int ret;

   if (!device || (!tx && !rx) || !words)
      return -EINVAL;

   ret = omap2_mcspi_configure(device);
   if (ret)
      return ret;

   channel = device->chip_select;
   conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(conf | MCSPI_CHCONF_FORCE,
          omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(MCSPI_CHCTRL_EN, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

   for (word = 0; word < words; word++) {
      ret = omap2_mcspi_wait(channel, MCSPI_CHSTAT_TXS);
      if (ret)
         goto out;

      writel(tx ? tx[word] : 0, omap2_mcspi_channel_reg(channel, MCSPI_TX0));

      ret = omap2_mcspi_wait(channel, MCSPI_CHSTAT_RXS);
      if (ret)
         goto out;

      if (rx)
         rx[word] = readl(omap2_mcspi_channel_reg(channel, MCSPI_RX0));
      else
         readl(omap2_mcspi_channel_reg(channel, MCSPI_RX0));
   }

out:
   conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(conf & ~MCSPI_CHCONF_FORCE,
          omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(0, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

   return ret;
}

/*
 * One CS assertion for the entire sequence. MIPID reads use a 9-bit command
 * followed by a 9-bit first receive word and 8-bit subsequent receive words.
 * Reconfigure only the channel word length between individual words.
 */
static int omap2_mcspi1_transfer_words_internal(const struct omap2_mcspi_device *device,
                                                 const struct omap2_mcspi_word *tx, u32 *rx,
                                                 size_t words,
                                                 struct omap2_mcspi_trace *trace)
{
   unsigned int channel;
   size_t word;
   u32 conf;
   u32 value;
   int ret;

   if (!device || !tx || !words)
      return -EINVAL;

   if (trace && (!trace->word || trace->words < words))
      return -EINVAL;

   ret = omap2_mcspi_configure(device);
   if (ret)
      return ret;

   channel = device->chip_select;

   if (trace) {
      trace->modulctrl = readl(mcspi1 + MCSPI_MODULCTRL);
      trace->chconf_initial = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
      trace->chctrl_initial = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));
      trace->words = words;
   }

   conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(conf | MCSPI_CHCONF_FORCE,
          omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(MCSPI_CHCTRL_EN, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

   for (word = 0; word < words; word++) {
      if (tx[word].bits_per_word < 4 || tx[word].bits_per_word > 32) {
         ret = -EINVAL;
         goto out;
      }

      ret = omap2_mcspi_wait(channel, MCSPI_CHSTAT_TXS);
      if (ret)
         goto out;

      /* Disable the channel before changing WL; keep FORCE asserted. */
      writel(0, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));
      conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
      conf &= ~MCSPI_CHCONF_WL_MASK;
      conf |= (tx[word].bits_per_word - 1) << MCSPI_CHCONF_WL_SHIFT;
      writel(conf, omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
      writel(MCSPI_CHCTRL_EN, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

      if (trace) {
         trace->word[word].chconf =
            readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
         trace->word[word].chctrl =
            readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));
         trace->word[word].chstat_before =
            readl(omap2_mcspi_channel_reg(channel, MCSPI_CHSTAT0));
         trace->word[word].tx = tx[word].tx;
         trace->word[word].bits_per_word = tx[word].bits_per_word;
      }

      writel(tx[word].tx, omap2_mcspi_channel_reg(channel, MCSPI_TX0));

      ret = omap2_mcspi_wait(channel, MCSPI_CHSTAT_RXS);
      if (ret)
         goto out;

      value = readl(omap2_mcspi_channel_reg(channel, MCSPI_RX0));
      if (rx)
         rx[word] = value;

      if (trace) {
         trace->word[word].rx = value;
         trace->word[word].chstat_after =
            readl(omap2_mcspi_channel_reg(channel, MCSPI_CHSTAT0));
      }
   }

out:
   conf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(conf & ~MCSPI_CHCONF_FORCE,
          omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   writel(0, omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

   return ret;
}

int omap2_mcspi1_transfer_words(const struct omap2_mcspi_device *device,
                                const struct omap2_mcspi_word *tx, u32 *rx, size_t words)
{
   return omap2_mcspi1_transfer_words_internal(device, tx, rx, words, NULL);
}

int omap2_mcspi1_transfer_words_trace(const struct omap2_mcspi_device *device,
                                      const struct omap2_mcspi_word *tx, u32 *rx,
                                      size_t words, struct omap2_mcspi_trace *trace)
{
   return omap2_mcspi1_transfer_words_internal(device, tx, rx, words, trace);
}

int omap2_mcspi1_get_channel_state(unsigned int channel,
                                   struct omap2_mcspi_channel_state *state)
{
   if (!state || channel > 3)
      return -EINVAL;

   state->modulctrl = readl(mcspi1 + MCSPI_MODULCTRL);
   state->chconf = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCONF0));
   state->chstat = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHSTAT0));
   state->chctrl = readl(omap2_mcspi_channel_reg(channel, MCSPI_CHCTRL0));

   return 0;
}
