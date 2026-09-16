// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <linux/sizes.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>

#define N800_SDRAM_BASE 0x80000000
#define N800_SDRAM_SIZE SZ_128M

extern char __dtb_z_omap2420_n800_start[];

/*
 * The Nokia N800 is entered as a second-stage bootloader.
 *
 * On real hardware, NOLO has already initialized SDRAM and loads this image
 * into RAM. QEMU provides the equivalent initial CPU/RAM environment.
 *
 * Do not initialize SDRAM here. Peripheral state inherited from NOLO must
 * not be relied upon by later drivers.
 */
ENTRY_FUNCTION_WITHSTACK(start_nokia_n800, N800_SDRAM_BASE + N800_SDRAM_SIZE, r0, r1, r2)
{
   arm_cpu_lowlevel_init();

   barebox_arm_entry(N800_SDRAM_BASE, N800_SDRAM_SIZE,
                     __dtb_z_omap2420_n800_start + get_runtime_offset());
}
