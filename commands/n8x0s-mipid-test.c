// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <clock.h>
#include <command.h>
#include <errno.h>
#include <io.h>
#include <linux/bitops.h>

#include "../drivers/video/n800-blizzard-core.h"
#include "../drivers/video/n800-display-power.h"
#include "../drivers/video/n800-mipid.h"

struct n800_mipid_read_test {
   const char *name;
   u8 command;
   u8 count;
};

static const struct n800_mipid_read_test n800_mipid_read_tests[] = {
   { "display status", 0x09, 4 },
   { "power mode", 0x0a, 1 },
   { "address mode", 0x0b, 1 },
   { "pixel format", 0x0c, 1 },
   { "image mode", 0x0d, 1 },
   { "signal mode", 0x0e, 1 },
   { "diagnostic", 0x0f, 1 },
};

static void n800_mipid_dump_trace(const struct n800_mipid_trace *trace)
{
   size_t i;

   printf("N800 MIPID McSPI1/CS1 trace:\n");
   printf("  MODULCTRL=%08x CHCONF1(initial)=%08x CHCTRL1(initial)=%08x\n",
          trace->spi.modulctrl, trace->spi.chconf_initial, trace->spi.chctrl_initial);

   for (i = 0; i < trace->spi.words; i++) {
      const struct omap2_mcspi_trace_word *word = &trace->word[i];

      printf("  [%zu] WL=%u TX=%08x RX=%08x CHCONF=%08x CHCTRL=%08x "
             "CHSTAT(before/after)=%08x/%08x\n",
             i, word->bits_per_word, word->tx, word->rx, word->chconf, word->chctrl,
             word->chstat_before, word->chstat_after);
   }
}

static int do_n8x0s_mipid_test(int argc, char *argv[])
{
   struct n800_mipid_trace trace;
   u8 value[N800_MIPID_MAX_READ_BYTES];
   u8 id[3];
   size_t i;
   size_t j;
   int ret;

   ret = n800_mipid_read_id_trace(id, ARRAY_SIZE(id), &trace);
   if (ret) {
      printf("N800 MIPID CS1 display ID read failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   n800_mipid_dump_trace(&trace);

   printf("N800 MIPID display ID: %02x %02x %02x\n", id[0], id[1], id[2]);
   switch (id[0]) {
      case 0x45:
         printf("Nokia panel: LPH8923, revision %02x\n", id[1]);
         break;
      case 0x83:
         printf("Nokia panel: LS041Y3, revision %02x\n", id[1]);
         break;
      default:
         printf("Unknown panel ID.\n");
         break;
   }

   printf("N800 MIPID read-only DCS registers:\n");
   for (i = 0; i < ARRAY_SIZE(n800_mipid_read_tests); i++) {
      const struct n800_mipid_read_test *test = &n800_mipid_read_tests[i];

      ret = n800_mipid_read(test->command, value, test->count);
      if (ret) {
         printf("  %02x %-14s: read failed: %pe\n", test->command, test->name,
                ERR_PTR(ret));
         return COMMAND_ERROR;
      }

      printf("  %02x %-14s:", test->command, test->name);
      for (j = 0; j < test->count; j++)
         printf(" %02x", value[j]);
      printf("\n");
   }

   printf("All accesses above are read-only; no panel state was changed.\n");

   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_mipid_test)
BAREBOX_CMD_HELP_TEXT("Read the LCD MIPID ID and read-only DCS status registers.")
BAREBOX_CMD_HELP_TEXT("Does not change panel state.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_mipid_test)
   .cmd = do_n8x0s_mipid_test,
   BAREBOX_CMD_DESC("test N800 LCD MIPID read-only access")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_mipid_test_help)
BAREBOX_CMD_END

static int do_n8x0s_mipid_reinit(int argc, char *argv[])
{
   u8 status[4];
   u8 power_mode;
   int ret;

   printf("N800 MIPID: reinitializing LS041Y3 panel\n");
   printf("  Display Off -> Sleep In -> 120 ms -> Sleep Out -> 120 ms\n");
   printf("  -> Nokia init C2(02 00 00) -> Pixel Format 3A(70) -> Display On\n");

   ret = n800_mipid_reinitialize_ls041y3();
   if (ret) {
      printf("N800 MIPID reinitialization failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   ret = n800_mipid_read(0x09, status, ARRAY_SIZE(status));
   if (ret) {
      printf("N800 MIPID display status read failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   ret = n800_mipid_read(0x0a, &power_mode, 1);
   if (ret) {
      printf("N800 MIPID power mode read failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   printf("N800 MIPID reinitialization complete.\n");
   printf("  display status: %02x %02x %02x %02x\n", status[0], status[1], status[2],
          status[3]);
   printf("  power mode    : %02x\n", power_mode);

   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_mipid_reinit)
BAREBOX_CMD_HELP_TEXT("Cycle and reinitialize the Nokia N800 LS041Y3 LCD panel.")
BAREBOX_CMD_HELP_TEXT("Uses the initialization sequence from Nokia's lcd_mipid driver.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_mipid_reinit)
   .cmd = do_n8x0s_mipid_reinit,
   BAREBOX_CMD_DESC("reinitialize N800 LS041Y3 LCD panel")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_mipid_reinit_help)
BAREBOX_CMD_END

/* Explicit diagnostic; no sleep/reset or other panel writes. */
static int do_n8x0s_mipid_lines(int argc, char *argv[])
{
   u8 before;
   u8 after;
   unsigned int lines;
   int ret;

   if (argc != 2)
      return COMMAND_ERROR_USAGE;

   ret = kstrtouint(argv[1], 10, &lines);
   if (ret || (lines != 16 && lines != 18 && lines != 24))
      return COMMAND_ERROR_USAGE;

   ret = n800_mipid_read(0x0c, &before, 1);
   if (ret)
      return COMMAND_ERROR;

   printf("MIPID: set %u data lines (pixel format before: %02x)\n", lines, before);
   ret = n800_mipid_set_data_lines(lines);
   if (ret) {
      printf("MIPID: write failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   ret = n800_mipid_read(0x0c, &after, 1);
   if (ret)
      return COMMAND_ERROR;

   printf("MIPID: pixel format after: %02x\n", after);
   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_mipid_lines)
BAREBOX_CMD_HELP_TEXT("Set the LS041Y3 data-line setting only: 16, 18 or 24.")
BAREBOX_CMD_HELP_TEXT("Experimental: use the original NOLO state as reference.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_mipid_lines)
   .cmd = do_n8x0s_mipid_lines,
   BAREBOX_CMD_DESC("test LS041Y3 data-line configuration")
   BAREBOX_CMD_OPTS("16|18|24")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_mipid_lines_help)
BAREBOX_CMD_END

/*
 * RX-34 display handoff snapshot. This deliberately does not request, remux,
 * drive or reset any GPIO. The addresses and offsets are from Nokia's
 * OMAP24xx GPIO driver (arch/arm/plat-omap/gpio.c).
 *
 * GPIO15 is the Blizzard POWERDOWN line on the N800. The LS041Y3 nRESET
 * GPIO is supplied by NOLO's OMAP_TAG_LCD; its number is not established
 * here, so this command does not guess or touch it.
 */
#define N800_GPIO1_BASE          0x48018000U
#define N800_GPIO_OE_OFFSET      0x0034U
#define N800_GPIO_DATAIN_OFFSET  0x0038U
#define N800_GPIO_DATAOUT_OFFSET 0x003cU
#define N800_BLIZZARD_PWDN_BIT   15U

static int do_n8x0s_display_handoff(int argc, char *argv[])
{
   void __iomem *gpio1 = IOMEM(N800_GPIO1_BASE);
   u32 oe;
   u32 datain;
   u32 dataout;
   u8 id[3];
   u8 status[4];
   u8 pixel_format;
   int ret;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   oe = readl(gpio1 + N800_GPIO_OE_OFFSET);
   datain = readl(gpio1 + N800_GPIO_DATAIN_OFFSET);
   dataout = readl(gpio1 + N800_GPIO_DATAOUT_OFFSET);

   printf("N800 display handoff snapshot (read-only):\n");
   printf("  GPIO1 OE      : %08x\n", oe);
   printf("  GPIO1 DATAIN  : %08x\n", datain);
   printf("  GPIO1 DATAOUT : %08x\n", dataout);
   printf("  GPIO15 Blizzard POWERDOWN: direction=%s input=%u output-latch=%u\n",
          (oe & BIT(N800_BLIZZARD_PWDN_BIT)) ? "input" : "output",
          !!(datain & BIT(N800_BLIZZARD_PWDN_BIT)),
          !!(dataout & BIT(N800_BLIZZARD_PWDN_BIT)));

   ret = n800_mipid_read_id(id, ARRAY_SIZE(id));
   if (ret)
      goto read_error;

   ret = n800_mipid_read(0x09, status, ARRAY_SIZE(status));
   if (ret)
      goto read_error;

   ret = n800_mipid_read(0x0c, &pixel_format, 1);
   if (ret)
      goto read_error;

   printf("  LS041Y3 ID     : %02x %02x %02x\n", id[0], id[1], id[2]);
   printf("  Display status : %02x %02x %02x %02x\n", status[0], status[1],
          status[2], status[3]);
   printf("  Pixel format   : %02x (24-line reference: e3)\n", pixel_format);
   printf("  LS041Y3 nRESET : unknown (NOLO OMAP_TAG_LCD; not accessed)\n");
   printf("No GPIO or display state was changed.\n");
   return 0;

read_error:
   printf("  MIPID read failed: %pe\n", ERR_PTR(ret));
   return COMMAND_ERROR;
}

BAREBOX_CMD_HELP_START(n8x0s_display_handoff)
BAREBOX_CMD_HELP_TEXT("Read the N800 display GPIO15 and LS041Y3 handoff state.")
BAREBOX_CMD_HELP_TEXT("Read-only: does not reset, remux or drive any pin.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_display_handoff)
   .cmd = do_n8x0s_display_handoff,
   BAREBOX_CMD_DESC("inspect N800 display handoff without changing hardware state")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_display_handoff_help)
BAREBOX_CMD_END

/*
 * Read-only OMAP2420 display-path snapshot. The offsets below match the
 * existing omap2-dispc.c and omap2-rfbi.c drivers in this tree.
 *
 * Do not read RFBI data/command registers: these can have side effects.
 * Do not touch the unknown LS041Y3 nRESET pin or GPIO15.
 */
#define N800_DISPC_BASE          0x48050400U
#define N800_RFBI_BASE           0x48050800U

struct n800_display_register {
   const char *name;
   u32 offset;
};

static const struct n800_display_register n800_dispc_snapshot[] = {
   { "CONTROL", 0x0040U },
   { "CONFIG", 0x0044U },
   { "DIVISOR", 0x0070U },
   { "SIZE_LCD", 0x007cU },
   { "GFX_BA0", 0x0080U },
   { "GFX_POSITION", 0x0088U },
   { "GFX_SIZE", 0x008cU },
   { "GFX_ATTRIBUTES", 0x00a0U },
   { "GFX_FIFO_THRESHOLD", 0x00a4U },
   { "GFX_FIFO_SIZE_STATUS", 0x00a8U },
   { "GFX_ROW_INC", 0x00acU },
};

static const struct n800_display_register n800_rfbi_snapshot[] = {
   { "SYSCONFIG", 0x0010U },
   { "SYSSTATUS", 0x0014U },
   { "CONTROL", 0x0040U },
   { "CONFIG0", 0x0060U },
   { "ONOFF_TIME0", 0x0064U },
   { "CYCLE_TIME0", 0x0068U },
   { "DATA_CYCLE1_0", 0x006cU },
   { "DATA_CYCLE2_0", 0x0070U },
   { "DATA_CYCLE3_0", 0x0074U },
   { "VSYNC_WIDTH", 0x0090U },
   { "HSYNC_WIDTH", 0x0094U },
};

static void n800_dump_display_registers(const char *block, u32 base,
                                        const struct n800_display_register *regs,
                                        size_t count)
{
   size_t i;

   printf("  %s:\n", block);
   for (i = 0; i < count; i++)
      printf("    %-20s %08x\n", regs[i].name,
             readl(IOMEM(base + regs[i].offset)));
}

static int do_n8x0s_display_path(int argc, char *argv[])
{
   u32 oe;
   u32 datain;
   u32 dataout;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   oe = readl(IOMEM(N800_GPIO1_BASE + N800_GPIO_OE_OFFSET));
   datain = readl(IOMEM(N800_GPIO1_BASE + N800_GPIO_DATAIN_OFFSET));
   dataout = readl(IOMEM(N800_GPIO1_BASE + N800_GPIO_DATAOUT_OFFSET));

   printf("N800 display path snapshot (read-only):\n");
   printf("  GPIO1 OE      : %08x\n", oe);
   printf("  GPIO1 DATAIN  : %08x\n", datain);
   printf("  GPIO1 DATAOUT : %08x\n", dataout);
   printf("  GPIO15 Blizzard POWERDOWN: direction=%s input=%u output-latch=%u\n",
          (oe & BIT(N800_BLIZZARD_PWDN_BIT)) ? "input" : "output",
          !!(datain & BIT(N800_BLIZZARD_PWDN_BIT)),
          !!(dataout & BIT(N800_BLIZZARD_PWDN_BIT)));

   n800_dump_display_registers("DISPC", N800_DISPC_BASE, n800_dispc_snapshot,
                                ARRAY_SIZE(n800_dispc_snapshot));
   n800_dump_display_registers("RFBI", N800_RFBI_BASE, n800_rfbi_snapshot,
                                ARRAY_SIZE(n800_rfbi_snapshot));
   printf("  LS041Y3 nRESET: GPIO number not yet established; not accessed\n");
   printf("No reset, GPIO write or display configuration was performed.\n");
   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_display_path)
BAREBOX_CMD_HELP_TEXT("Snapshot GPIO15, DISPC and RFBI registers without writes.")
BAREBOX_CMD_HELP_TEXT("Compare before and after MIPID reinit and framebuffer activity.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_display_path)
   .cmd = do_n8x0s_display_path,
   BAREBOX_CMD_DESC("read-only N800 display-path register snapshot")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_display_path_help)
BAREBOX_CMD_END

/*
 * Step 12: Read Blizzard controller registers through the existing RFBI bus.
 * The RFBI command/read cycle changes its transaction registers but does not
 * write any Blizzard configuration register or toggle any reset/power pin.
 * Run only from an idle Barebox prompt, not during a framebuffer transfer.
 */
#include "../drivers/video/omap2-rfbi.h"

static const struct n800_display_register n800_blizzard_snapshot[] = {
   { "REV_CODE", 0x00U },
   { "CONFIG", 0x02U },
   { "PLL_DIV", 0x04U },
   { "PLL_CLOCK_SYNTH_0", 0x08U },
   { "PLL_CLOCK_SYNTH_1", 0x0aU },
   { "CLK_SRC", 0x0eU },
   { "HDISP", 0x2aU },
   { "HNDP", 0x2cU },
   { "VDISP0", 0x2eU },
   { "VDISP1", 0x30U },
   { "VNDP", 0x32U },
   { "HSW", 0x34U },
   { "VSW", 0x38U },
   { "NDISP_CTRL_STATUS", 0xe8U },
};

static int do_n8x0s_blizzard_snapshot(int argc, char *argv[])
{
   u32 rfbi_control;
   u32 rfbi_config;
   size_t i;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   rfbi_control = readl(IOMEM(N800_RFBI_BASE + 0x40U));
   rfbi_config = readl(IOMEM(N800_RFBI_BASE + 0x60U));

   printf("N800 Blizzard register snapshot (RFBI reads):\n");
   printf("  RFBI CONTROL before: %08x\n", rfbi_control);
   printf("  RFBI CONFIG0 before: %08x\n", rfbi_config);
   if (rfbi_control & BIT(0)) {
      printf("  RFBI transfer active; refusing to access Blizzard.\n");
      return COMMAND_ERROR;
   }

   for (i = 0; i < ARRAY_SIZE(n800_blizzard_snapshot); i++) {
      const struct n800_display_register *reg = &n800_blizzard_snapshot[i];

      printf("  %-20s [0x%02x] = %02x\n", reg->name, reg->offset,
             omap2_rfbi_read_reg8(reg->offset));
   }

   /* omap2_rfbi_read_reg8() selects the 8-bit RFBI mode. Restore the exact
    * entry value so this diagnostic remains transparent to later transfers.
    */
   writel(rfbi_config, IOMEM(N800_RFBI_BASE + 0x60U));
   printf("  RFBI CONFIG0 after : %08x (restored)\n",
          readl(IOMEM(N800_RFBI_BASE + 0x60U)));
   printf("No Blizzard configuration register or GPIO was written.\n");
   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_blizzard_snapshot)
BAREBOX_CMD_HELP_TEXT("Read Blizzard identification, PLL and display geometry over RFBI.")
BAREBOX_CMD_HELP_TEXT("Run at an idle prompt; no power/reset pins are changed.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_blizzard_snapshot)
   .cmd = do_n8x0s_blizzard_snapshot,
   BAREBOX_CMD_DESC("read N800 Blizzard controller registers")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_blizzard_snapshot_help)
BAREBOX_CMD_END


/*
 * Step 13: Replay the writable part of the known-good S1D13745 state measured
 * on the real RX-34 in Step 12.  This deliberately does not touch GPIO15,
 * panel reset, REV_CODE or CONFIG.  RFBI CONFIG0 is restored afterwards.
 *
 * The purpose is to validate the Blizzard write path before the first power
 * cycle.  Values are identical to the live NOLO-provided state, so this is
 * still not a cold initialization sequence.
 */
struct n800_blizzard_reference_register {
   const char *name;
   u8 reg;
   u8 value;
};

static const struct n800_blizzard_reference_register n800_blizzard_reference[] = {
   { "PLL_DIV", 0x04U, 0x92U },
   { "PLL_CLOCK_SYNTH_0", 0x08U, 0x42U },
   { "PLL_CLOCK_SYNTH_1", 0x0aU, 0x00U },
   { "CLK_SRC", 0x0eU, 0x11U },
   { "HDISP", 0x2aU, 0x64U },
   { "HNDP", 0x2cU, 0x1eU },
   { "VDISP0", 0x2eU, 0xe0U },
   { "VDISP1", 0x30U, 0x01U },
   { "VNDP", 0x32U, 0x06U },
   { "HSW", 0x34U, 0x14U },
   { "VSW", 0x38U, 0x02U },
   { "NDISP_CTRL_STATUS", 0xe8U, 0x02U },
};

static void n800_blizzard_write_reg8(u8 reg, u8 value)
{
   omap2_rfbi_write_command8(reg);
   omap2_rfbi_write_data8(value);
}

static int do_n8x0s_blizzard_replay(int argc, char *argv[])
{
   u32 rfbi_control;
   u32 rfbi_config;
   u8 revision;
   size_t i;
   int ret = 0;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   rfbi_control = readl(IOMEM(N800_RFBI_BASE + 0x40U));
   rfbi_config = readl(IOMEM(N800_RFBI_BASE + 0x60U));

   printf("N800 Blizzard known-good state replay:\n");
   printf("  RFBI CONTROL before: %08x\n", rfbi_control);
   printf("  RFBI CONFIG0 before: %08x\n", rfbi_config);
   if (rfbi_control & BIT(0)) {
      printf("  RFBI transfer active; refusing to write Blizzard.\n");
      return COMMAND_ERROR;
   }

   revision = omap2_rfbi_read_reg8(0x00U);
   if ((revision & 0xfcU) != 0xa4U) {
      printf("  Unexpected Blizzard revision %02x; refusing replay.\n", revision);
      ret = COMMAND_ERROR;
      goto restore_rfbi;
   }

   printf("  S1D13745 revision: %02x\n", revision);
   for (i = 0; i < ARRAY_SIZE(n800_blizzard_reference); i++) {
      const struct n800_blizzard_reference_register *reg =
         &n800_blizzard_reference[i];
      u8 before;
      u8 after;

      before = omap2_rfbi_read_reg8(reg->reg);
      if (before != reg->value) {
         printf("  %-20s [0x%02x]: current=%02x expected=%02x; refusing replay.\n",
                reg->name, reg->reg, before, reg->value);
         ret = COMMAND_ERROR;
         goto restore_rfbi;
      }

      n800_blizzard_write_reg8(reg->reg, reg->value);
      after = omap2_rfbi_read_reg8(reg->reg);
      printf("  %-20s [0x%02x]: %02x -> %02x\n",
             reg->name, reg->reg, before, after);
      if (after != reg->value) {
         printf("  Readback mismatch; stopping replay.\n");
         ret = COMMAND_ERROR;
         goto restore_rfbi;
      }
   }

restore_rfbi:
   writel(rfbi_config, IOMEM(N800_RFBI_BASE + 0x60U));
   printf("  RFBI CONFIG0 after : %08x (restored)\n",
          readl(IOMEM(N800_RFBI_BASE + 0x60U)));
   if (!ret)
      printf("Known-good Blizzard register replay completed. No GPIO was changed.\n");

   return ret;
}

BAREBOX_CMD_HELP_START(n8x0s_blizzard_replay)
BAREBOX_CMD_HELP_TEXT("Replay and verify the Step-12 known-good S1D13745 register state.")
BAREBOX_CMD_HELP_TEXT("Refuses to run if any reference register differs before writing.")
BAREBOX_CMD_HELP_TEXT("Does not toggle POWERDOWN or panel reset.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_blizzard_replay)
   .cmd = do_n8x0s_blizzard_replay,
   BAREBOX_CMD_DESC("replay known-good N800 Blizzard register state")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_blizzard_replay_help)
BAREBOX_CMD_END


/*
 * Step 15: Capture the complete Blizzard state that Nokia's RX-34 Linux
 * driver saved/restored across suspend/resume.  The original driver does not
 * contain a cold-init register table; it explicitly requires the bootloader
 * to have initialized the controller.  Capture the missing state before we
 * attempt to replace that bootloader responsibility.
 */
static void n800_blizzard_dump_range(u8 first, u8 last)
{
   unsigned int reg;

   for (reg = first; reg <= last; reg += 2U)
      printf("  [0x%02x] = %02x\n", reg, omap2_rfbi_read_reg8(reg));
}

static int do_n8x0s_blizzard_cold_baseline(int argc, char *argv[])
{
   u32 rfbi_control;
   u32 rfbi_config;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   rfbi_control = readl(IOMEM(N800_RFBI_BASE + 0x40U));
   rfbi_config = readl(IOMEM(N800_RFBI_BASE + 0x60U));

   printf("N800 Blizzard runtime baseline (read-only):\n");
   printf("  RFBI CONTROL before: %08x\n", rfbi_control);
   printf("  RFBI CONFIG0 before: %08x\n", rfbi_config);
   if (rfbi_control & BIT(0)) {
      printf("  RFBI transfer active; refusing to access Blizzard.\n");
      return COMMAND_ERROR;
   }

   printf("Identification and PLL/clock registers:\n");
   n800_blizzard_dump_range(0x00U, 0x0eU);

   printf("SDRAM registers saved by Nokia Linux (0x18..0x20):\n");
   n800_blizzard_dump_range(0x18U, 0x20U);

   printf("Panel/general registers saved by Nokia Linux (0x28..0x56):\n");
   n800_blizzard_dump_range(0x28U, 0x56U);
   printf("  0x58/0x5a skipped: indirect TV-filter pair has read side effects.\n");

   printf("Additional runtime/power registers:\n");
   printf("  DISPLAY_MODE         [0x68] = %02x\n", omap2_rfbi_read_reg8(0x68U));
   printf("  DATA_SOURCE_SELECT   [0x8e] = %02x\n", omap2_rfbi_read_reg8(0x8eU));
   printf("  POWER_SAVE           [0xe6] = %02x\n", omap2_rfbi_read_reg8(0xe6U));
   printf("  NDISP_CTRL_STATUS    [0xe8] = %02x\n", omap2_rfbi_read_reg8(0xe8U));

   writel(rfbi_config, IOMEM(N800_RFBI_BASE + 0x60U));
   printf("  RFBI CONFIG0 after : %08x (restored)\n",
          readl(IOMEM(N800_RFBI_BASE + 0x60U)));
   printf("No Blizzard register, GPIO or power-control register was written.\n");

   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_blizzard_cold_baseline)
BAREBOX_CMD_HELP_TEXT("Capture the inherited NOLO Blizzard runtime state.")
BAREBOX_CMD_HELP_TEXT("Includes Nokia Linux PLL, SDRAM and general save/restore ranges.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_blizzard_cold_baseline)
   .cmd = do_n8x0s_blizzard_cold_baseline,
   BAREBOX_CMD_DESC("capture N800 Blizzard runtime baseline")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_blizzard_cold_baseline_help)
BAREBOX_CMD_END


/*
 * Runtime PM smoke test. The actual controller state machine now lives in
 * n800-blizzard-core.c and board POWERDOWN handling in n800-display-power.c.
 * This command only composes those production-layer operations for hardware
 * verification.
 */
static void n800_blizzard_test_set_bits(void *context, unsigned int bits)
{
   omap2_rfbi_set_bits_per_cycle(bits);
}

static u8 n800_blizzard_test_read_reg(void *context, u8 reg)
{
   return omap2_rfbi_read_reg8(reg);
}

static void n800_blizzard_test_write_command(void *context, u8 command)
{
   omap2_rfbi_write_command8(command);
}

static void n800_blizzard_test_write_data8(void *context, u8 value)
{
   omap2_rfbi_write_data8(value);
}

static int do_n8x0s_blizzard_pm_test(int argc, char *argv[])
{
   struct n800_blizzard_bus bus = {
      .set_bits_per_cycle = n800_blizzard_test_set_bits,
      .read_reg = n800_blizzard_test_read_reg,
      .write_command = n800_blizzard_test_write_command,
      .write_data8 = n800_blizzard_test_write_data8,
   };
   u32 rfbi_control;
   u32 rfbi_config;
   int ret;

   if (argc != 1)
      return COMMAND_ERROR_USAGE;

   rfbi_control = readl(IOMEM(N800_RFBI_BASE + 0x40U));
   rfbi_config = readl(IOMEM(N800_RFBI_BASE + 0x60U));

   printf("N800 Blizzard runtime PM test:\n");
   if (rfbi_control & BIT(0)) {
      printf("  RFBI transfer active; refusing suspend.\n");
      return COMMAND_ERROR;
   }

   printf("  suspend controller\n");
   ret = n800_blizzard_suspend(&bus);
   if (ret)
      goto out;

   printf("  assert board POWERDOWN\n");
   ret = n800_display_set_powerdown(true);
   if (ret)
      goto recover_controller;

   printf("  release board POWERDOWN\n");
   ret = n800_display_set_powerdown(false);
   if (ret)
      goto out;

   printf("  restore RX-34 display power/clock\n");
   ret = n800_display_power_up();
   if (ret)
      goto out;

   printf("  resume controller\n");
   ret = n800_blizzard_resume(&bus);
   goto out;

recover_controller:
   n800_display_set_powerdown(false);
   n800_display_power_up();
   n800_blizzard_resume(&bus);
out:
   writel(rfbi_config, IOMEM(N800_RFBI_BASE + 0x60U));
   printf("  RFBI CONFIG0 after: %08x (restored)\n",
          readl(IOMEM(N800_RFBI_BASE + 0x60U)));
   if (ret) {
      printf("N800 Blizzard runtime PM test failed: %pe\n", ERR_PTR(ret));
      return COMMAND_ERROR;
   }

   printf("N800 Blizzard runtime PM test completed.\n");
   return 0;
}

BAREBOX_CMD_HELP_START(n8x0s_blizzard_pm_test)
BAREBOX_CMD_HELP_TEXT("Exercise production-layer Blizzard suspend/resume and board POWERDOWN.")
BAREBOX_CMD_HELP_TEXT("NOLO remains responsible for initial S1D13745 cold initialization.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_blizzard_pm_test)
   .cmd = do_n8x0s_blizzard_pm_test,
   BAREBOX_CMD_DESC("test N800 Blizzard runtime power management")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_n8x0s_blizzard_pm_test_help)
BAREBOX_CMD_END
