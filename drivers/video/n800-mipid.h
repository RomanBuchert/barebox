/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/types.h>
#include <mach/omap/omap2-mcspi.h>

#define N800_MIPID_ID_TRANSFER_WORDS 5

struct n800_mipid_id_trace {
   u32 rx[N800_MIPID_ID_TRANSFER_WORDS];
   struct omap2_mcspi_trace_word word[N800_MIPID_ID_TRANSFER_WORDS];
   struct omap2_mcspi_trace spi;
};

/* Read-only MIPID Display ID; does not change the panel state. */
int n800_mipid_read_id(u8 *response, size_t count);
int n800_mipid_read_id_trace(u8 *response, size_t count, struct n800_mipid_id_trace *trace);
