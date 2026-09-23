/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <linux/types.h>
#include <mach/omap/omap2-mcspi.h>

#define N800_MIPID_MAX_READ_BYTES 4
#define N800_MIPID_MAX_READ_WORDS (N800_MIPID_MAX_READ_BYTES + 1)

struct n800_mipid_trace {
   u32 rx[N800_MIPID_MAX_READ_WORDS];
   struct omap2_mcspi_trace_word word[N800_MIPID_MAX_READ_WORDS];
   struct omap2_mcspi_trace spi;
};

/* Read-only MIPID/DCS access; does not change the panel state. */
int n800_mipid_read(u8 command, u8 *response, size_t count);
int n800_mipid_read_trace(u8 command, u8 *response, size_t count,
                          struct n800_mipid_trace *trace);
int n800_mipid_read_id(u8 *response, size_t count);
int n800_mipid_read_id_trace(u8 *response, size_t count, struct n800_mipid_trace *trace);

/* MIPID/DCS write access. Data bytes are transmitted with the 9th D/C bit set. */
int n800_mipid_command(u8 command);
int n800_mipid_write(u8 command, const u8 *data, size_t count);

/* Reinitialize the LS041Y3 panel using the sequence from Nokia's lcd_mipid driver. */
int n800_mipid_reinitialize_ls041y3(void);

int n800_mipid_set_data_lines(unsigned int lines);
