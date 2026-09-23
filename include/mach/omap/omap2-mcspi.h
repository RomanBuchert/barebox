/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/bitops.h>
#include <linux/types.h>

#define OMAP2_MCSPI_MODE_0 0
#define OMAP2_MCSPI_MODE_1 BIT(0)
#define OMAP2_MCSPI_MODE_2 BIT(1)
#define OMAP2_MCSPI_MODE_3 (BIT(0) | BIT(1))

struct omap2_mcspi_device {
   unsigned int chip_select;
   unsigned int mode;
   unsigned int bits_per_word;
   unsigned int max_speed_hz;
};

int omap2_mcspi1_setup(void);
int omap2_mcspi1_transfer(const struct omap2_mcspi_device *device, const u32 *tx,
                          u32 *rx, size_t words);

/* Transfer one word with CS already asserted; word length may change per word. */
struct omap2_mcspi_word {
   u32 tx;
   u8 bits_per_word;
};

struct omap2_mcspi_trace_word {
   u32 chconf;
   u32 chctrl;
   u32 chstat_before;
   u32 chstat_after;
   u32 tx;
   u32 rx;
   u8 bits_per_word;
};

struct omap2_mcspi_trace {
   u32 modulctrl;
   u32 chconf_initial;
   u32 chctrl_initial;
   size_t words;
   struct omap2_mcspi_trace_word *word;
};

int omap2_mcspi1_transfer_words(const struct omap2_mcspi_device *device,
                               const struct omap2_mcspi_word *tx, u32 *rx, size_t words);

int omap2_mcspi1_transfer_words_trace(const struct omap2_mcspi_device *device,
                                      const struct omap2_mcspi_word *tx, u32 *rx,
                                      size_t words, struct omap2_mcspi_trace *trace);

struct omap2_mcspi_channel_state {
   u32 modulctrl;
   u32 chconf;
   u32 chstat;
   u32 chctrl;
};

int omap2_mcspi1_get_channel_state(unsigned int channel,
                                   struct omap2_mcspi_channel_state *state);
