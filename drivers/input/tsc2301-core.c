// SPDX-License-Identifier: GPL-2.0-only
/*
 * TSC2301 register access core.
 *
 * Keep the wire protocol here and functional policy in the client driver.
 * This makes the verified register sequence easy to reproduce in Linux later.
 */

#include <errno.h>
#include <input/tsc2301.h>
#include <linux/bitops.h>

#define TSC2301_READ                     BIT(15)
#define TSC2301_COMMAND(read, page, reg) ((read) | ((page) << 11) | ((reg) << 5))

u16 tsc2301_read_command(u8 page, u8 reg)
{
   return TSC2301_COMMAND(TSC2301_READ, page, reg);
}

int tsc2301_read_reg(struct tsc2301 *tsc, u8 page, u8 reg, u16 *value)
{
   if (!tsc || !tsc->transfer || !value)
      return -EINVAL;

   return tsc->transfer(tsc->context, tsc2301_read_command(page, reg), 0, value);
}

int tsc2301_write_reg(struct tsc2301 *tsc, u8 page, u8 reg, u16 value)
{
   u16 dummy;

   if (!tsc || !tsc->transfer)
      return -EINVAL;

   return tsc->transfer(tsc->context, TSC2301_COMMAND(0, page, reg), value, &dummy);
}
