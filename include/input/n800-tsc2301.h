/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <input/tsc2301.h>
#include <mach/omap/omap2-mcspi.h>

/* Nokia RX-34 TSC2301 wiring/configuration from the original Nokia kernel. */
#define N800_TSC2301_SPI_MODE         OMAP2_MCSPI_MODE_0
#define N800_TSC2301_SPI_BITS         16U
#define N800_TSC2301_SPI_MAX_SPEED_HZ 6000000U

int n800_tsc2301_init(struct tsc2301 *tsc);
