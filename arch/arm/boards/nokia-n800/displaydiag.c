// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nokia N800 display bring-up diagnostics.
 *
 * This command intentionally lives in the N800 board directory. It is a
 * bring-up tool, not a generic OMAP display API. Read-only commands are meant
 * to inspect the state inherited from NOLO before changing display hardware.
 */

#include <command.h>
#include <common.h>
#include <errno.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <i2c/i2c-cbus-gpio.h>

#define OMAP2420_DSS_BASE        0x48050000U
#define OMAP2420_DISPC_BASE      0x48050400U
#define OMAP2420_RFBI_BASE       0x48050800U

#define OMAP2420_CM_FCLKEN1_CORE 0x48008200U
#define OMAP2420_CM_ICLKEN1_CORE 0x48008210U
#define OMAP2420_CM_CLKSEL1_CORE 0x48008240U
#define OMAP2420_CM_CLKSEL2_CORE 0x48008244U

#define RFBI_CMD                 0x004cU
#define RFBI_PARAM               0x0050U
#define RFBI_DATA                0x0054U
#define RFBI_READ                0x0058U
#define RFBI_STATUS              0x005cU
#define RFBI_CONTROL             0x0040U
#define RFBI_CONFIG0             0x0060U
#define RFBI_ONOFF_TIME0          0x0064U
#define RFBI_CYCLE_TIME0          0x0068U
#define RFBI_DATA_CYCLE1_0        0x006cU
#define RFBI_DATA_CYCLE2_0        0x0070U
#define RFBI_DATA_CYCLE3_0        0x0074U

#define RFBI_PARALLEL_MODE_MASK   0x00000003U
#define RFBI_PARALLEL_MODE_8      0x00000000U
#define RFBI_PARALLEL_MODE_16     0x00000003U

#define BLIZZARD_INPUT_WIN_X_START_0 0x006cU
#define BLIZZARD_COLOR_RGB565         0x01U
#define BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE 0x01U

#define N800_RECT_X                350U
#define N800_RECT_Y                210U
#define N800_RECT_WIDTH            100U
#define N800_RECT_HEIGHT            60U
#define N800_RECT_COLOR_RGB565   0xf800U

#define OMAP2420_CONTROL_BASE     0x48000000U
#define OMAP2420_PADCONF_DSS_D5   0x00b8U
#define OMAP2420_PADCONF_DSS_D9   0x00bcU
#define OMAP2420_PADCONF_DSS_D13  0x00c0U
#define OMAP2420_PADCONF_DSS_VSYNC 0x00ccU
#define OMAP2420_PADCONF_GPIO15   0x00e7U

#define OMAP2420_GPIO1_BASE       0x48018000U
#define OMAP_GPIO_OE              0x0034U
#define OMAP_GPIO_DATAIN          0x0038U
#define OMAP_GPIO_DATAOUT         0x003cU
#define N800_BLIZZARD_PWRDN_BIT   15U
#define N800_BLIZZARD_RESET_BIT   30U

#define TAHVO_CBUS_ID               0x02U
#define TAHVO_REG_VCORE             0x07U
#define TAHVO_VCORE_MASK            0x000fU

#define OMAP2420_PRCM_BASE          0x48008000U
#define PRCM_CLKSRC_CTRL            0x0060U
#define PRCM_OSC_DISABLE_MASK       (0x3U << 3)

#define BLIZZARD_REV_CODE            0x00U
#define BLIZZARD_CONFIG              0x02U
#define BLIZZARD_PLL_DIV             0x04U

#define BLIZZARD_PLL_CLOCK_SYNTH_0   0x08U
#define BLIZZARD_PLL_CLOCK_SYNTH_1   0x0aU
#define BLIZZARD_CLK_SRC             0x0eU

#define OMAP2420_CM_CLKSEL1_PLL_ADDR 0x48008540U
#define OMAP2420_CM_CLKSEL2_PLL_ADDR 0x48008544U

#define RFBI_SYSCONFIG               0x0010U
#define RFBI_SYSSTATUS               0x0014U

#define DISPC_SYSCONFIG              0x0010U
#define DISPC_SYSSTATUS              0x0014U
#define DISPC_CONTROL                0x0040U
#define DISPC_CONFIG                 0x0044U

#define V113_TEST_X                  350U
#define V113_TEST_Y                  210U
#define V113_TEST_WIDTH              100U
#define V113_TEST_HEIGHT             60U
#define V113_TEST_RGB565             0xf800U

struct v113_extif_timings {
   int cs_on_time;
   int cs_off_time;
   int we_on_time;
   int we_off_time;
   int re_on_time;
   int re_off_time;
   int we_cycle_time;
   int re_cycle_time;
   int cs_pulse_width;
   int access_time;
   int clk_div;
   u32 tim[3];
   bool converted;
};

/*
 * Historical N800 RFBI register-access configuration.
 *
 * Historical/reference Linux RFBI/Blizzard configuration used during earlier
 * bring-up work. The execution environment of the old test log is not known
 * with sufficient confidence, so these values are not treated as a verified
 * real-N800 hardware reference.
 *
 * For bpp=16 over an 8-bit bus the historical RFBI bus configuration uses
 * two data cycles per pixel: DATA_CYCLE1=8, DATA_CYCLE2=8, DATA_CYCLE3=0.
 */
#define RFBI_HIST_CONFIG0          0x00300220U
#define RFBI_HIST_ONOFF_TIME0      0x03108440U
#define RFBI_HIST_CYCLE_TIME0      0x00c00104U
#define RFBI_HIST_DATA_CYCLE1_0    0x00000008U
#define RFBI_HIST_DATA_CYCLE2_0    0x00000008U
#define RFBI_HIST_DATA_CYCLE3_0    0x00000000U

#define RFBI_CONTROL_BYPASS        (1U << 1)
#define RFBI_CONTROL_CS_MASK       (3U << 2)
#define RFBI_CONTROL_CS0           (1U << 2)

struct displaydiag_reg {
   const char *name;
   u32 offset;
};

struct displaydiag_block {
   const char *name;
   u32 base;
   const struct displaydiag_reg *regs;
   size_t num_regs;
};

static const struct displaydiag_reg dss_regs[] = {
   { "REVISION",  0x0000 },
   { "SYSCONFIG", 0x0010 },
   { "SYSSTATUS", 0x0014 },
   { "CONTROL",   0x0040 },
   { "SDI_CONTROL", 0x0044 },
   { "PLL_CONTROL", 0x0048 },
};

static const struct displaydiag_reg dispc_regs[] = {
   { "REVISION",       0x0000 },
   { "SYSCONFIG",      0x0010 },
   { "SYSSTATUS",      0x0014 },
   { "IRQSTATUS",      0x0018 },
   { "IRQENABLE",      0x001c },
   { "CONTROL",        0x0040 },
   { "CONFIG",         0x0044 },
   { "CAPABLE",        0x0048 },
   { "DEFAULT_COLOR0", 0x004c },
   { "DEFAULT_COLOR1", 0x0050 },
   { "TRANS_COLOR0",   0x0054 },
   { "TRANS_COLOR1",   0x0058 },
   { "LINE_STATUS",    0x005c },
   { "LINE_NUMBER",    0x0060 },
   { "TIMING_H",       0x0064 },
   { "TIMING_V",       0x0068 },
   { "POL_FREQ",       0x006c },
   { "DIVISOR",        0x0070 },
};

static const struct displaydiag_reg rfbi_regs[] = {
   { "REVISION",      0x0000 },
   { "SYSCONFIG",     0x0010 },
   { "SYSSTATUS",     0x0014 },
   { "CONTROL",       0x0040 },
   { "PIXEL_CNT",     0x0044 },
   { "LINE_NUMBER",   0x0048 },
   { "CMD",           0x004c },
   { "PARAM",         0x0050 },
   { "DATA",          0x0054 },
   { "READ",          0x0058 },
   { "STATUS",        0x005c },
   { "CONFIG0",       0x0060 },
   { "ONOFF_TIME0",   0x0064 },
   { "CYCLE_TIME0",   0x0068 },
   { "DATA_CYCLE1_0", 0x006c },
   { "DATA_CYCLE2_0", 0x0070 },
   { "DATA_CYCLE3_0", 0x0074 },
   { "CONFIG1",       0x0078 },
   { "ONOFF_TIME1",   0x007c },
   { "CYCLE_TIME1",   0x0080 },
   { "DATA_CYCLE1_1", 0x0084 },
   { "DATA_CYCLE2_1", 0x0088 },
   { "DATA_CYCLE3_1", 0x008c },
   { "VSYNC_WIDTH",   0x0090 },
   { "HSYNC_WIDTH",   0x0094 },
};

static const struct displaydiag_block blocks[] = {
   { "dss", OMAP2420_DSS_BASE, dss_regs, ARRAY_SIZE(dss_regs) },
   { "dispc", OMAP2420_DISPC_BASE, dispc_regs, ARRAY_SIZE(dispc_regs) },
   { "rfbi", OMAP2420_RFBI_BASE, rfbi_regs, ARRAY_SIZE(rfbi_regs) },
};

static u32 displaydiag_readl(u32 address)
{
   return readl((void __iomem *)(unsigned long)address);
}

static void displaydiag_writel(u32 value, u32 address)
{
   writel(value, (void __iomem *)(unsigned long)address);
}

static u8 displaydiag_readb(u32 address)
{
   return readb((void __iomem *)(unsigned long)address);
}

static const struct displaydiag_block *displaydiag_find_block(const char *name)
{
   size_t i;

   for (i = 0; i < ARRAY_SIZE(blocks); i++) {
      if (!strcmp(name, blocks[i].name))
         return &blocks[i];
   }

   return NULL;
}

static const struct displaydiag_reg *displaydiag_find_reg(const struct displaydiag_block *block,
                                                           const char *name)
{
   size_t i;

   for (i = 0; i < block->num_regs; i++) {
      if (!strcasecmp(name, block->regs[i].name))
         return &block->regs[i];
   }

   return NULL;
}

static int displaydiag_parse_u32(const char *text, u32 *value)
{
   unsigned long parsed;
   char *endp;

   parsed = simple_strtoul(text, &endp, 0);
   if (!text[0] || *endp != '\0' || parsed > 0xffffffffUL)
      return -EINVAL;

   *value = (u32)parsed;
   return 0;
}

static int displaydiag_resolve_reg(const struct displaydiag_block *block,
                                   const char *text,
                                   u32 *offset,
                                   const char **name)
{
   const struct displaydiag_reg *reg;
   u32 parsed;

   reg = displaydiag_find_reg(block, text);
   if (reg) {
      *offset = reg->offset;
      *name = reg->name;
      return 0;
   }

   if (displaydiag_parse_u32(text, &parsed))
      return -EINVAL;

   /*
    * Do not permit arbitrary MMIO offsets here. Some OMAP2420 display
    * register holes generate an external abort instead of returning a
    * harmless value. Numeric offsets are therefore accepted only when
    * they exactly match a register in the verified table above.
    */
   for (size_t i = 0; i < block->num_regs; i++) {
      if (parsed == block->regs[i].offset) {
         *offset = parsed;
         *name = block->regs[i].name;
         return 0;
      }
   }

   return -EINVAL;
}

static void displaydiag_print_reg(const struct displaydiag_block *block,
                                  const struct displaydiag_reg *reg)
{
   u32 address = block->base + reg->offset;

   printf("  %-18s @ 0x%08x = 0x%08x\n",
          reg->name, address, displaydiag_readl(address));
}

static void displaydiag_dump_block(const struct displaydiag_block *block)
{
   size_t i;

   printf("%s @ 0x%08x\n", block->name, block->base);
   for (i = 0; i < block->num_regs; i++)
      displaydiag_print_reg(block, &block->regs[i]);
}

static void displaydiag_dump_clocks(void)
{
   printf("OMAP2420 display-related CORE clocks\n");
   printf("  CM_FCLKEN1_CORE @ 0x%08x = 0x%08x\n",
          OMAP2420_CM_FCLKEN1_CORE, displaydiag_readl(OMAP2420_CM_FCLKEN1_CORE));
   printf("  CM_ICLKEN1_CORE @ 0x%08x = 0x%08x\n",
          OMAP2420_CM_ICLKEN1_CORE, displaydiag_readl(OMAP2420_CM_ICLKEN1_CORE));
   printf("  CM_CLKSEL1_CORE @ 0x%08x = 0x%08x\n",
          OMAP2420_CM_CLKSEL1_CORE, displaydiag_readl(OMAP2420_CM_CLKSEL1_CORE));
   printf("  CM_CLKSEL2_CORE @ 0x%08x = 0x%08x\n",
          OMAP2420_CM_CLKSEL2_CORE, displaydiag_readl(OMAP2420_CM_CLKSEL2_CORE));
}

static u8 displaydiag_blizzard_read(u8 reg)
{
   displaydiag_writel(reg, OMAP2420_RFBI_BASE + RFBI_CMD);
   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_READ);
   return displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ) & 0xff;
}

static void displaydiag_blizzard_write(u8 reg, u8 value)
{
   displaydiag_writel(reg, OMAP2420_RFBI_BASE + RFBI_CMD);
   displaydiag_writel(value, OMAP2420_RFBI_BASE + RFBI_PARAM);
}

static void displaydiag_dump_blizzard(void)
{
   u8 revision;
   u8 config;
   u8 pll_div;
   u8 ndisp;

   revision = displaydiag_blizzard_read(0x00);
   config = displaydiag_blizzard_read(0x02);
   pll_div = displaydiag_blizzard_read(0x04);
   ndisp = displaydiag_blizzard_read(0xe8);

   printf("Epson S1D13745 Blizzard via RFBI\n");
   printf("  REV_CODE           [0x00] = 0x%02x\n", revision);
   printf("  CONFIG             [0x02] = 0x%02x\n", config);
   printf("  PLL_DIV            [0x04] = 0x%02x\n", pll_div);
   printf("  NDISP_CTRL_STATUS  [0xe8] = 0x%02x\n", ndisp);
   printf("  expected N800 reference: REV_CODE=0xa5, CONFIG=0x83\n");
}

static void displaydiag_blizzard_bus_snapshot(const char *label)
{
   printf("  %-20s CMD=0x%08x PARAM=0x%08x DATA=0x%08x READ=0x%08x STATUS=0x%08x\n",
          label,
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CMD),
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_PARAM),
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_DATA),
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ),
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_STATUS));
}

static int displaydiag_blizzard_read_debug_one(u8 reg)
{
   u32 control_before;
   u32 config0_before;
   u32 config0_8bit;
   u32 config0_restored;
   u32 control_after;
   u32 cmd_after;
   u32 read_after;

   control_before = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);
   config0_before = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   config0_8bit = (config0_before & ~RFBI_PARALLEL_MODE_MASK) |
                  RFBI_PARALLEL_MODE_8;

   printf("Blizzard RFBI bus-cycle debug\n");
   printf("  register             : 0x%02x\n", reg);
   printf("  CONTROL original     : 0x%08x\n", control_before);
   printf("  CONFIG0 original     : 0x%08x\n", config0_before);
   printf("  CONFIG0 8-bit        : 0x%08x\n", config0_8bit);
   displaydiag_blizzard_bus_snapshot("before switch");

   displaydiag_writel(config0_8bit, OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   printf("  CONFIG0 active       : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   displaydiag_blizzard_bus_snapshot("after switch");

   displaydiag_writel(reg, OMAP2420_RFBI_BASE + RFBI_CMD);
   cmd_after = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CMD);
   printf("  CMD write/readback   : 0x%02x -> 0x%08x\n", reg, cmd_after);
   displaydiag_blizzard_bus_snapshot("after CMD");

   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_READ);
   displaydiag_blizzard_bus_snapshot("after READ trigger");

   read_after = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ);
   printf("  READ second sample   : 0x%08x\n", read_after);
   displaydiag_blizzard_bus_snapshot("after READ sample");
   printf("  result               : 0x%02x\n", read_after & 0xff);

   displaydiag_writel(config0_before, OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   config0_restored = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   control_after = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);
   printf("  CONFIG0 restored     : 0x%08x\n", config0_restored);
   printf("  CONTROL after        : 0x%08x\n", control_after);

   if (config0_restored != config0_before)
      printf("  WARNING: CONFIG0 restore verification failed\n");
   if (control_after != control_before)
      printf("  WARNING: CONTROL changed during transaction\n");

   return read_after & 0xff;
}

struct displaydiag_rfbi_state {
   u32 control;
   u32 config0;
   u32 onoff_time0;
   u32 cycle_time0;
   u32 data_cycle1_0;
   u32 data_cycle2_0;
   u32 data_cycle3_0;
};

static void displaydiag_rfbi_save_state(struct displaydiag_rfbi_state *state)
{
   state->control = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);
   state->config0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   state->onoff_time0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_ONOFF_TIME0);
   state->cycle_time0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CYCLE_TIME0);
   state->data_cycle1_0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE1_0);
   state->data_cycle2_0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE2_0);
   state->data_cycle3_0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE3_0);
}

static void displaydiag_rfbi_print_state(const char *label,
                                         const struct displaydiag_rfbi_state *state)
{
   printf("%s\n", label);
   printf("  CONTROL       : 0x%08x\n", state->control);
   printf("  CONFIG0       : 0x%08x\n", state->config0);
   printf("  ONOFF_TIME0   : 0x%08x\n", state->onoff_time0);
   printf("  CYCLE_TIME0   : 0x%08x\n", state->cycle_time0);
   printf("  DATA_CYCLE1_0 : 0x%08x\n", state->data_cycle1_0);
   printf("  DATA_CYCLE2_0 : 0x%08x\n", state->data_cycle2_0);
   printf("  DATA_CYCLE3_0 : 0x%08x\n", state->data_cycle3_0);
}

static void displaydiag_rfbi_read_state(struct displaydiag_rfbi_state *state)
{
   displaydiag_rfbi_save_state(state);
}

static void displaydiag_rfbi_select_none(u32 control_base)
{
   displaydiag_writel(control_base & ~RFBI_CONTROL_CS_MASK,
                      OMAP2420_RFBI_BASE + RFBI_CONTROL);
}

static void displaydiag_rfbi_select_cs0(u32 control_base)
{
   u32 control;

   control = control_base & ~RFBI_CONTROL_CS_MASK;
   control |= RFBI_CONTROL_CS0;
   control &= ~RFBI_CONTROL_BYPASS;
   displaydiag_writel(control, OMAP2420_RFBI_BASE + RFBI_CONTROL);
}

static void displaydiag_rfbi_apply_historical_register_mode(u32 control_base)
{
   /* Match the historical rfbi_configure_bus() ordering. */
   displaydiag_rfbi_select_none(control_base);

   displaydiag_writel(RFBI_HIST_CONFIG0,
                      OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   displaydiag_writel(RFBI_HIST_DATA_CYCLE1_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE1_0);
   displaydiag_writel(RFBI_HIST_DATA_CYCLE2_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE2_0);
   displaydiag_writel(RFBI_HIST_DATA_CYCLE3_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE3_0);

   displaydiag_rfbi_select_cs0(control_base);

   displaydiag_writel(RFBI_HIST_ONOFF_TIME0,
                      OMAP2420_RFBI_BASE + RFBI_ONOFF_TIME0);
   displaydiag_writel(RFBI_HIST_CYCLE_TIME0,
                      OMAP2420_RFBI_BASE + RFBI_CYCLE_TIME0);
}

static void displaydiag_rfbi_restore_state(const struct displaydiag_rfbi_state *state)
{
   /* Avoid driving a chip select while restoring bus format/timings. */
   displaydiag_rfbi_select_none(state->control);

   displaydiag_writel(state->config0,
                      OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   displaydiag_writel(state->onoff_time0,
                      OMAP2420_RFBI_BASE + RFBI_ONOFF_TIME0);
   displaydiag_writel(state->cycle_time0,
                      OMAP2420_RFBI_BASE + RFBI_CYCLE_TIME0);
   displaydiag_writel(state->data_cycle1_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE1_0);
   displaydiag_writel(state->data_cycle2_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE2_0);
   displaydiag_writel(state->data_cycle3_0,
                      OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE3_0);

   displaydiag_writel(state->control,
                      OMAP2420_RFBI_BASE + RFBI_CONTROL);
}

static bool displaydiag_rfbi_state_equal(const struct displaydiag_rfbi_state *a,
                                         const struct displaydiag_rfbi_state *b)
{
   return a->control == b->control &&
          a->config0 == b->config0 &&
          a->onoff_time0 == b->onoff_time0 &&
          a->cycle_time0 == b->cycle_time0 &&
          a->data_cycle1_0 == b->data_cycle1_0 &&
          a->data_cycle2_0 == b->data_cycle2_0 &&
          a->data_cycle3_0 == b->data_cycle3_0;
}

static int displaydiag_cmd_blizzard_historical_read_test(int argc, char *argv[])
{
   struct displaydiag_rfbi_state original;
   struct displaydiag_rfbi_state active;
   struct displaydiag_rfbi_state restored;
   u32 cmd_after;
   u32 read_after;
   u8 revision;

   if (argc != 2)
      return COMMAND_ERROR_USAGE;

   printf("N800 Blizzard historical RFBI register-read test\n");
   printf("================================================\n");
   printf("This temporarily installs the verified historical N800 RFBI\n");
   printf("register-access configuration, reads Blizzard REV_CODE, then\n");
   printf("restores the complete inherited RFBI channel-0 state.\n\n");

   displaydiag_rfbi_save_state(&original);
   displaydiag_rfbi_print_state("Inherited state:", &original);

   displaydiag_rfbi_apply_historical_register_mode(original.control);
   displaydiag_rfbi_read_state(&active);
   printf("\n");
   displaydiag_rfbi_print_state("Historical register mode active:", &active);

   printf("\nBlizzard REV_CODE transaction:\n");
   displaydiag_writel(0x00, OMAP2420_RFBI_BASE + RFBI_CMD);
   cmd_after = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CMD);
   printf("  CMD write/readback : 0x00 -> 0x%08x\n", cmd_after);

   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_READ);
   read_after = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ);
   revision = read_after & 0xff;
   printf("  READ              : 0x%08x\n", read_after);
   printf("  REV_CODE          : 0x%02x", revision);
   if (revision == 0xa5)
      printf("  (expected S1D13745 value)\n");
   else
      printf("  (expected 0xa5)\n");

   displaydiag_rfbi_restore_state(&original);
   displaydiag_rfbi_read_state(&restored);
   printf("\n");
   displaydiag_rfbi_print_state("Restored inherited state:", &restored);

   if (!displaydiag_rfbi_state_equal(&original, &restored)) {
      printf("\nWARNING: RFBI state restore verification FAILED\n");
      return -EIO;
   }

   printf("\nRFBI state restore verification: OK\n");
   return 0;
}

static int displaydiag_cmd_blizzard_read_debug(int argc, char *argv[])
{
   u32 reg;

   if (argc != 3)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_parse_u32(argv[2], &reg) || reg > 0xff)
      return COMMAND_ERROR_USAGE;

   displaydiag_blizzard_read_debug_one((u8)reg);
   return 0;
}

static int displaydiag_cmd_blizzard_bus_debug(int argc, char *argv[])
{
   static const u8 regs[] = { 0x00, 0x02, 0x04, 0xe8 };
   size_t i;

   if (argc != 2)
      return COMMAND_ERROR_USAGE;

   printf("N800 Blizzard RFBI bus-cycle diagnostic\n");
   printf("========================================\n");
   printf("Each access temporarily selects 8-bit parallel mode and restores CONFIG0.\n");
   printf("No Blizzard register is written. RFBI CMD/READ cycles are generated.\n\n");

   for (i = 0; i < ARRAY_SIZE(regs); i++) {
      displaydiag_blizzard_read_debug_one(regs[i]);
      if (i + 1 < ARRAY_SIZE(regs))
         printf("\n");
   }

   return 0;
}

static int displaydiag_cmd_read(int argc, char *argv[])
{
   const struct displaydiag_block *block;
   const char *name;
   u32 offset;
   u32 address;
   u32 value;

   if (argc != 4)
      return COMMAND_ERROR_USAGE;

   if (!strcmp(argv[2], "blizzard")) {
      if (displaydiag_parse_u32(argv[3], &offset) || offset > 0xff)
         return COMMAND_ERROR_USAGE;

      printf("blizzard[0x%02x] = 0x%02x\n", offset,
             displaydiag_blizzard_read((u8)offset));
      return 0;
   }

   block = displaydiag_find_block(argv[2]);
   if (!block)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_resolve_reg(block, argv[3], &offset, &name))
      return COMMAND_ERROR_USAGE;

   address = block->base + offset;
   value = displaydiag_readl(address);

   if (name)
      printf("%s.%s @ 0x%08x = 0x%08x\n", block->name, name, address, value);
   else
      printf("%s+0x%03x @ 0x%08x = 0x%08x\n", block->name, offset, address, value);

   return 0;
}

static int displaydiag_cmd_write(int argc, char *argv[])
{
   const struct displaydiag_block *block;
   const char *name;
   u32 offset;
   u32 address;
   u32 old_value;
   u32 value;

   if (argc != 5)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_parse_u32(argv[4], &value))
      return COMMAND_ERROR_USAGE;

   if (!strcmp(argv[2], "blizzard")) {
      if (displaydiag_parse_u32(argv[3], &offset) || offset > 0xff || value > 0xff)
         return COMMAND_ERROR_USAGE;

      old_value = displaydiag_blizzard_read((u8)offset);
      printf("WARNING: direct Blizzard register write\n");
      printf("blizzard[0x%02x]: 0x%02x -> 0x%02x\n", offset, old_value, value);
      displaydiag_blizzard_write((u8)offset, (u8)value);
      return 0;
   }

   block = displaydiag_find_block(argv[2]);
   if (!block)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_resolve_reg(block, argv[3], &offset, &name))
      return COMMAND_ERROR_USAGE;

   address = block->base + offset;
   old_value = displaydiag_readl(address);

   printf("WARNING: direct hardware register write\n");
   if (name)
      printf("%s.%s @ 0x%08x: 0x%08x -> 0x%08x\n",
             block->name, name, address, old_value, value);
   else
      printf("%s+0x%03x @ 0x%08x: 0x%08x -> 0x%08x\n",
             block->name, offset, address, old_value, value);

   displaydiag_writel(value, address);
   return 0;
}

static int displaydiag_cmd_modify(int argc, char *argv[])
{
   const struct displaydiag_block *block;
   const char *name;
   u32 offset;
   u32 address;
   u32 clear_mask;
   u32 set_mask;
   u32 old_value;
   u32 new_value;

   if (argc != 6)
      return COMMAND_ERROR_USAGE;

   if (!strcmp(argv[2], "blizzard")) {
      if (displaydiag_parse_u32(argv[3], &offset) || offset > 0xff ||
          displaydiag_parse_u32(argv[4], &clear_mask) || clear_mask > 0xff ||
          displaydiag_parse_u32(argv[5], &set_mask) || set_mask > 0xff)
         return COMMAND_ERROR_USAGE;

      old_value = displaydiag_blizzard_read((u8)offset);
      new_value = (old_value & ~clear_mask) | set_mask;
      new_value &= 0xff;

      printf("WARNING: direct Blizzard register modification\n");
      printf("blizzard[0x%02x]: old=0x%02x clear=0x%02x set=0x%02x new=0x%02x\n",
             offset, old_value, clear_mask, set_mask, new_value);
      displaydiag_blizzard_write((u8)offset, (u8)new_value);
      return 0;
   }

   block = displaydiag_find_block(argv[2]);
   if (!block)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_resolve_reg(block, argv[3], &offset, &name) ||
       displaydiag_parse_u32(argv[4], &clear_mask) ||
       displaydiag_parse_u32(argv[5], &set_mask))
      return COMMAND_ERROR_USAGE;

   address = block->base + offset;
   old_value = displaydiag_readl(address);
   new_value = (old_value & ~clear_mask) | set_mask;

   printf("WARNING: direct hardware register modification\n");
   printf("  old        : 0x%08x\n", old_value);
   printf("  clear mask : 0x%08x\n", clear_mask);
   printf("  set mask   : 0x%08x\n", set_mask);
   printf("  new        : 0x%08x\n", new_value);
   displaydiag_writel(new_value, address);

   return 0;
}

static int displaydiag_cmd_pixel_test(int argc, char *argv[])
{
   u32 pixel;
   u32 count;
   u32 config0;
   u32 control;
   u32 i;

   if (argc != 4)
      return COMMAND_ERROR_USAGE;

   if (displaydiag_parse_u32(argv[2], &pixel) || pixel > 0xffff ||
       displaydiag_parse_u32(argv[3], &count) || count == 0 || count > 384000)
      return COMMAND_ERROR_USAGE;

   config0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   control = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);

   printf("N800 inherited RFBI pixel write test\n");
   printf("================================\n");
   printf("  CONFIG0 : 0x%08x\n", config0);
   printf("  CONTROL : 0x%08x\n", control);
   printf("  PARAM   : 0x%08x\n", OMAP2420_RFBI_BASE + RFBI_PARAM);
   printf("  RGB565  : 0x%04x\n", pixel);
   printf("  count   : %u\n", count);
   printf("\n");
   printf("No RFBI or Blizzard configuration register will be changed.\n");
   printf("Writing %u values to RFBI_PARAM...\n", count);

   for (i = 0; i < count; i++)
      displaydiag_writel(pixel, OMAP2420_RFBI_BASE + RFBI_PARAM);

   printf("Pixel write test completed.\n");

   return 0;
}


static int displaydiag_cmd_pinmux(void)
{
   u8 gpio15_padconf;

   gpio15_padconf = displaydiag_readb(OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_GPIO15);

   printf("N800 display pinmux diagnostic\n");
   printf("==============================\n");
   printf("Read-only. Register selection is limited to padconf locations named by\n");
   printf("the supplied RX-34 Linux 2.6.21.0 source.\n\n");

   printf("OMAP2420 DSS padconf registers (raw 32-bit values):\n");
   printf("  DSS_D5    @ 0x%08x = 0x%08x\n",
          OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D5,
          displaydiag_readl(OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D5));
   printf("  DSS_D9    @ 0x%08x = 0x%08x\n",
          OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D9,
          displaydiag_readl(OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D9));
   printf("  DSS_D13   @ 0x%08x = 0x%08x\n",
          OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D13,
          displaydiag_readl(OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_D13));
   printf("  DSS_VSYNC @ 0x%08x = 0x%08x\n",
          OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_VSYNC,
          displaydiag_readl(OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_DSS_VSYNC));

   printf("\nGPIO15 padconf (8-bit mux register):\n");
   printf("  GPIO15    @ 0x%08x = 0x%02x\n",
          OMAP2420_CONTROL_BASE + OMAP2420_PADCONF_GPIO15, gpio15_padconf);
   printf("  mux mode              = %u\n", gpio15_padconf & 0x07U);
   printf("  RX-34 source GPIO mode = 3\n");

   printf("\nScope note:\n");
   printf("  The supplied .orig kernel tree names DSS_D5, DSS_D9, DSS_D13 and\n");
   printf("  DSS_VSYNC padconf registers, but does not contain the Nokia N800\n");
   printf("  board/Blizzard driver patches. Unnamed LCD/RFBI pad offsets are not\n");
   printf("  guessed by this diagnostic.\n");

   return 0;
}

static u8 displaydiag_blizzard_read8(u8 reg)
{
   u32 config0_before;
   u8 value;

   config0_before = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   displaydiag_writel(config0_before & ~RFBI_PARALLEL_MODE_MASK,
                      OMAP2420_RFBI_BASE + RFBI_CONFIG0);

   displaydiag_writel(reg, OMAP2420_RFBI_BASE + RFBI_CMD);
   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_READ);
   value = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ) & 0xffU;

   displaydiag_writel(config0_before, OMAP2420_RFBI_BASE + RFBI_CONFIG0);

   return value;
}


static unsigned long v113_round_to_tick(unsigned long ps,
                                        unsigned long extif_period_ps,
                                        int div)
{
   unsigned long tick = extif_period_ps * div;

   return (ps + tick - 1U) / tick * tick;
}

static int v113_ps_to_rfbi_ticks(int time_ps, unsigned long l4_khz, int div)
{
   unsigned long tick_ps;

   tick_ps = 1000000000UL / l4_khz * div;
   return (time_ps + tick_ps - 1U) / tick_ps;
}

static int v113_convert_timings(struct v113_extif_timings *t,
                                unsigned long l4_khz)
{
   int reon, reoff, weon, weoff, cson, csoff;
   int cs_pulse, actim, recyc, wecyc;
   u32 l;
   int div = t->clk_div;

   if (div <= 0 || div > 2)
      return -EINVAL;

   weon = v113_ps_to_rfbi_ticks(t->we_on_time, l4_khz, div);
   weoff = v113_ps_to_rfbi_ticks(t->we_off_time, l4_khz, div);
   if (weoff <= weon)
      weoff = weon + 1;
   if (weon > 0x0f || weoff > 0x3f)
      return -ERANGE;

   reon = v113_ps_to_rfbi_ticks(t->re_on_time, l4_khz, div);
   reoff = v113_ps_to_rfbi_ticks(t->re_off_time, l4_khz, div);
   if (reoff <= reon)
      reoff = reon + 1;
   if (reon > 0x0f || reoff > 0x3f)
      return -ERANGE;

   cson = v113_ps_to_rfbi_ticks(t->cs_on_time, l4_khz, div);
   csoff = v113_ps_to_rfbi_ticks(t->cs_off_time, l4_khz, div);
   if (csoff <= cson)
      csoff = cson + 1;
   if (csoff < max(weoff, reoff))
      csoff = max(weoff, reoff);
   if (cson > 0x0f || csoff > 0x3f)
      return -ERANGE;

   l = cson;
   l |= csoff << 4;
   l |= weon << 10;
   l |= weoff << 14;
   l |= reon << 20;
   l |= reoff << 24;
   t->tim[0] = l;

   actim = v113_ps_to_rfbi_ticks(t->access_time, l4_khz, div);
   if (actim <= reon)
      actim = reon + 1;
   if (actim > 0x3f)
      return -ERANGE;

   wecyc = v113_ps_to_rfbi_ticks(t->we_cycle_time, l4_khz, div);
   if (wecyc < weoff)
      wecyc = weoff;
   if (wecyc > 0x3f)
      return -ERANGE;

   recyc = v113_ps_to_rfbi_ticks(t->re_cycle_time, l4_khz, div);
   if (recyc < reoff)
      recyc = reoff;
   if (recyc > 0x3f)
      return -ERANGE;

   cs_pulse = v113_ps_to_rfbi_ticks(t->cs_pulse_width, l4_khz, div);
   if (cs_pulse > 0x3f)
      return -ERANGE;

   l = wecyc;
   l |= recyc << 6;
   l |= cs_pulse << 12;
   l |= actim << 22;
   t->tim[1] = l;
   t->tim[2] = div - 1;
   t->converted = true;

   return 0;
}

static int v113_calc_reg_timing(unsigned long sysclk,
                                unsigned long extif_period_ps,
                                unsigned long l4_khz, int div,
                                struct v113_extif_timings *t)
{
   unsigned long systim;

   memset(t, 0, sizeof(*t));
   systim = 1000000000UL / (sysclk / 1000UL);
   t->clk_div = div;

   t->cs_on_time = 0;
   t->we_on_time = v113_round_to_tick(t->cs_on_time + 2000,
                                      extif_period_ps, div);
   t->re_on_time = v113_round_to_tick(t->cs_on_time + 2000,
                                      extif_period_ps, div);
   t->access_time = v113_round_to_tick(t->re_on_time + 12200,
                                       extif_period_ps, div);
   t->we_off_time = v113_round_to_tick(t->we_on_time + 1000,
                                       extif_period_ps, div);
   t->re_off_time = v113_round_to_tick(t->re_on_time + 13000,
                                       extif_period_ps, div);
   t->cs_off_time = v113_round_to_tick(t->re_off_time + 1000,
                                       extif_period_ps, div);
   t->we_cycle_time = v113_round_to_tick(2 * systim + 2000,
                                         extif_period_ps, div);
   if (t->we_cycle_time < t->we_off_time)
      t->we_cycle_time = t->we_off_time;
   t->re_cycle_time = v113_round_to_tick(2 * systim + 2000,
                                         extif_period_ps, div);
   if (t->re_cycle_time < t->re_off_time)
      t->re_cycle_time = t->re_off_time;

   return v113_convert_timings(t, l4_khz);
}

static int v113_calc_lut_timing(unsigned long sysclk,
                                unsigned long extif_period_ps,
                                unsigned long l4_khz, int div,
                                struct v113_extif_timings *t)
{
   unsigned long systim;

   memset(t, 0, sizeof(*t));
   systim = 1000000000UL / (sysclk / 1000UL);
   t->clk_div = div;

   t->cs_on_time = 0;
   t->we_on_time = v113_round_to_tick(t->cs_on_time + 2000,
                                      extif_period_ps, div);
   t->re_on_time = v113_round_to_tick(t->cs_on_time + 2000,
                                      extif_period_ps, div);
   t->access_time = v113_round_to_tick(t->re_on_time + 4 * systim + 26000,
                                       extif_period_ps, div);
   t->we_off_time = v113_round_to_tick(t->we_on_time + 1000,
                                       extif_period_ps, div);
   t->re_off_time = v113_round_to_tick(t->re_on_time + 4 * systim + 26000,
                                       extif_period_ps, div);
   t->cs_off_time = v113_round_to_tick(t->re_off_time + 1000,
                                       extif_period_ps, div);
   t->we_cycle_time = v113_round_to_tick(2 * systim + 2000,
                                         extif_period_ps, div);
   if (t->we_cycle_time < t->we_off_time)
      t->we_cycle_time = t->we_off_time;
   t->re_cycle_time = v113_round_to_tick(2000 + 4 * systim + 26000,
                                         extif_period_ps, div);
   if (t->re_cycle_time < t->re_off_time)
      t->re_cycle_time = t->re_off_time;

   return v113_convert_timings(t, l4_khz);
}

static int v113_calc_extif_timings(unsigned long sysclk,
                                   unsigned long l4_khz,
                                   struct v113_extif_timings *reg,
                                   struct v113_extif_timings *lut,
                                   int *extif_mem_div)
{
   unsigned long extif_period_ps;
   int div;
   int ret;

   extif_period_ps = 1000000000UL / l4_khz;

   for (div = 1; div <= 2; div++) {
      ret = v113_calc_reg_timing(sysclk, extif_period_ps, l4_khz, div, reg);
      if (!ret)
         break;
   }
   if (div > 2)
      return -ERANGE;
   *extif_mem_div = div;

   for (div = 1; div <= 2; div++) {
      ret = v113_calc_lut_timing(sysclk, extif_period_ps, l4_khz, div, lut);
      if (!ret)
         break;
   }
   if (div > 2)
      return -ERANGE;

   return 0;
}

static void v113_set_rfbi_timings(const struct v113_extif_timings *t)
{
   u32 l;

   displaydiag_writel(t->tim[0], OMAP2420_RFBI_BASE + RFBI_ONOFF_TIME0);
   displaydiag_writel(t->tim[1], OMAP2420_RFBI_BASE + RFBI_CYCLE_TIME0);

   l = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   l &= ~(1U << 4);
   l |= (t->tim[2] ? 1U : 0U) << 4;
   displaydiag_writel(l, OMAP2420_RFBI_BASE + RFBI_CONFIG0);
}

static void v113_log_timing(const char *name,
                            const struct v113_extif_timings *t)
{
   printf("  %s clk_div=%d\n", name, t->clk_div);
   printf("    cs_on/off     : %d / %d ps\n", t->cs_on_time, t->cs_off_time);
   printf("    we_on/off     : %d / %d ps\n", t->we_on_time, t->we_off_time);
   printf("    re_on/off     : %d / %d ps\n", t->re_on_time, t->re_off_time);
   printf("    access        : %d ps\n", t->access_time);
   printf("    we/re cycle   : %d / %d ps\n",
          t->we_cycle_time, t->re_cycle_time);
   printf("    ONOFF_TIME0   : 0x%08x\n", t->tim[0]);
   printf("    CYCLE_TIME0   : 0x%08x\n", t->tim[1]);
   printf("    CONFIG divider: %u\n", t->tim[2]);
}

static int v113_read_clock_tree(unsigned long *osc_hz,
                                unsigned long *sys_hz,
                                unsigned long *dpll_hz,
                                unsigned long *l3_hz,
                                unsigned long *l4_hz)
{
   u32 clksrc;
   u32 pll1;
   u32 pll2;
   u32 core;
   u32 crystal_sel;
   u32 sys_div;
   u32 mult;
   u32 div;
   u32 amult;
   u32 l3_div;
   u32 l4_div;
   unsigned long crystal;

   clksrc = displaydiag_readl(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   pll1 = displaydiag_readl(OMAP2420_CM_CLKSEL1_PLL_ADDR);
   pll2 = displaydiag_readl(OMAP2420_CM_CLKSEL2_PLL_ADDR);
   core = displaydiag_readl(OMAP2420_CM_CLKSEL1_CORE);

   crystal_sel = (pll1 >> 23) & 0x7U;
   switch (crystal_sel) {
   case 0:
      crystal = 19200000UL;
      break;
   case 2:
      crystal = 13000000UL;
      break;
   case 3:
      crystal = 12000000UL;
      break;
   default:
      printf("  ERROR: unsupported RX-34 crystal selector %u\n", crystal_sel);
      return -EINVAL;
   }

   sys_div = (clksrc >> 6) & 0x3U;
   if (!sys_div) {
      printf("  ERROR: PRCM_CLKSRC_CTRL sysclk divider field is zero\n");
      return -EINVAL;
   }

   *sys_hz = crystal;
   *osc_hz = crystal * sys_div;

   mult = (pll1 >> 12) & 0x3ffU;
   div = (pll1 >> 8) & 0x0fU;
   amult = pll2 & 0x3U;
   if (!amult) {
      printf("  ERROR: CM_CLKSEL2_PLL DPLL output multiplier is zero\n");
      return -EINVAL;
   }

   /*
    * All RX-34 clock values and intermediate products fit in 32 bits.
    * Keep this calculation 32-bit so ARMv6 Barebox does not require the
    * libgcc __udivdi3 helper for an unsigned 64-bit division.
    */
   *dpll_hz = (*sys_hz * mult) / (div + 1U);
   *dpll_hz *= amult;

   l3_div = core & 0x1fU;
   l4_div = (core >> 5) & 0x3U;
   if (!l3_div || !l4_div) {
      printf("  ERROR: invalid L3/L4 divider (%u/%u)\n", l3_div, l4_div);
      return -EINVAL;
   }

   *l3_hz = *dpll_hz / l3_div;
   *l4_hz = *l3_hz / l4_div;

   printf("  PRCM_CLKSRC_CTRL : 0x%08x\n", clksrc);
   printf("  CM_CLKSEL1_PLL   : 0x%08x\n", pll1);
   printf("  CM_CLKSEL2_PLL   : 0x%08x\n", pll2);
   printf("  CM_CLKSEL1_CORE  : 0x%08x\n", core);
   printf("  crystal selector : %u\n", crystal_sel);
   printf("  sys_ck           : %lu Hz\n", *sys_hz);
   printf("  osc_ck           : %lu Hz\n", *osc_hz);
   printf("  DPLL             : %lu Hz\n", *dpll_hz);
   printf("  L3 divider/rate  : %u / %lu Hz\n", l3_div, *l3_hz);
   printf("  L4 divider/rate  : %u / %lu Hz\n", l4_div, *l4_hz);
   printf("  dss_ick          : %lu Hz (follows l4_ck in osso71)\n", *l4_hz);

   return 0;
}

static void v114_enable_rfbi_mode(void)
{
   u32 l;

   /*
    * Nokia osso71 drivers/video/omap/dispc.c: enable_rfbi_mode(1).
    * Preserve unrelated NOLO-programmed DISPC state and change only the
    * fields changed by the historical Nokia helper.
    */
   l = displaydiag_readl(OMAP2420_DISPC_BASE + DISPC_CONTROL);
   printf("  DISPC_CONTROL before : 0x%08x\n", l);

   l &= ~((1U << 11) | (1U << 15) | (1U << 16));
   l |= (1U << 11) | (1U << 15);
   displaydiag_writel(l, OMAP2420_DISPC_BASE + DISPC_CONTROL);

   l = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);
   printf("  RFBI_CONTROL before  : 0x%08x\n", l);
   l &= ~(1U << 1);
   displaydiag_writel(l, OMAP2420_RFBI_BASE + RFBI_CONTROL);

   printf("  DISPC_CONTROL after  : 0x%08x\n",
          displaydiag_readl(OMAP2420_DISPC_BASE + DISPC_CONTROL));
   printf("  RFBI_CONTROL after   : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));
}

static int v113_wait_rfbi_reset(void)
{
   unsigned int timeout;

   for (timeout = 0; timeout < 100; timeout++) {
      if (displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_SYSSTATUS) & 1U)
         return 0;
      mdelay(1);
   }

   return -ETIMEDOUT;
}

static void v113_rfbi_init(void)
{
   u32 l;

   displaydiag_writel(1U << 1, OMAP2420_RFBI_BASE + RFBI_SYSCONFIG);

   l = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_SYSCONFIG);
   l |= (1U << 0) | (2U << 3);
   displaydiag_writel(l, OMAP2420_RFBI_BASE + RFBI_SYSCONFIG);

   l = (0x03U << 0) | (0x00U << 2) | (0x01U << 5) | (0x02U << 7);
   l |= (0U << 9) | (1U << 20) | (1U << 21);
   displaydiag_writel(l, OMAP2420_RFBI_BASE + RFBI_CONFIG0);

   displaydiag_writel(0x10U, OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE1_0);
   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE2_0);
   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_DATA_CYCLE3_0);

   displaydiag_writel(1U << 2, OMAP2420_RFBI_BASE + RFBI_CONTROL);
}

static void v113_set_bits_per_cycle(unsigned int bpc)
{
   u32 l;

   l = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   l &= ~0x3U;
   if (bpc == 16)
      l |= 3U;
   displaydiag_writel(l, OMAP2420_RFBI_BASE + RFBI_CONFIG0);
}

static u8 v113_blizzard_read(u8 reg)
{
   u8 value;

   v113_set_bits_per_cycle(8);
   displaydiag_writel(reg, OMAP2420_RFBI_BASE + RFBI_CMD);
   displaydiag_writel(0, OMAP2420_RFBI_BASE + RFBI_READ);
   value = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_READ) & 0xffU;

   return value;
}

static void v113_set_window(u16 x, u16 y, u16 width, u16 height)
{
   u16 x_end = x + width - 1U;
   u16 y_end = y + height - 1U;
   u8 data[18];
   size_t i;

   data[0] = x;
   data[1] = x >> 8;
   data[2] = y;
   data[3] = y >> 8;
   data[4] = x_end;
   data[5] = x_end >> 8;
   data[6] = y_end;
   data[7] = y_end >> 8;
   data[8] = x;
   data[9] = x >> 8;
   data[10] = y;
   data[11] = y >> 8;
   data[12] = x_end;
   data[13] = x_end >> 8;
   data[14] = y_end;
   data[15] = y_end >> 8;
   data[16] = BLIZZARD_COLOR_RGB565;
   data[17] = BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE;

   v113_set_bits_per_cycle(8);
   displaydiag_writel(BLIZZARD_INPUT_WIN_X_START_0,
                      OMAP2420_RFBI_BASE + RFBI_CMD);
   for (i = 0; i < ARRAY_SIZE(data); i++)
      displaydiag_writel(data[i], OMAP2420_RFBI_BASE + RFBI_PARAM);
}

static int displaydiag_cmd_nokia_init_v114(void)
{
   struct v113_extif_timings reg_timing;
   struct v113_extif_timings lut_timing;
   unsigned long osc_hz, sys_hz, dpll_hz, l3_hz, l4_hz;
   unsigned long blizzard_sys_hz, pix_hz;
   unsigned int sys_div, sys_mul, pix_div;
   u32 clksrc;
   u32 l;
   int extif_div;
   int tahvo;
   int ret;
   u8 pll_div;
   u8 clk_src;
   u8 synth0;
   u8 synth1;
   u8 rev;
   u8 conf;
   unsigned int i;

   printf("N800 RX-34 source-faithful display bring-up V1.14\n");
   printf("================================================\n");
   printf("Destructive diagnostic. RFBI is reset and initialized from source.\n");
   printf("No inherited RFBI timing register is reused.\n\n");

   printf("[1] Reconstruct OMAP2420 clocks from PRCM\n");
   ret = v113_read_clock_tree(&osc_hz, &sys_hz, &dpll_hz, &l3_hz, &l4_hz);
   if (ret)
      return ret;
   printf("\n");

   printf("[2] Enable DSS interface/function clocks used by RFBI\n");
   l = displaydiag_readl(OMAP2420_CM_ICLKEN1_CORE);
   displaydiag_writel(l | 1U, OMAP2420_CM_ICLKEN1_CORE);
   l = displaydiag_readl(OMAP2420_CM_FCLKEN1_CORE);
   displaydiag_writel(l | 1U, OMAP2420_CM_FCLKEN1_CORE);
   printf("  CM_ICLKEN1_CORE : 0x%08x\n",
          displaydiag_readl(OMAP2420_CM_ICLKEN1_CORE));
   printf("  CM_FCLKEN1_CORE : 0x%08x\n\n",
          displaydiag_readl(OMAP2420_CM_FCLKEN1_CORE));

   printf("[3] Nokia DISPC external/RFBI mode preparation\n");
   printf("  DISPC_SYSCONFIG      : 0x%08x\n",
          displaydiag_readl(OMAP2420_DISPC_BASE + DISPC_SYSCONFIG));
   printf("  DISPC_SYSSTATUS      : 0x%08x\n",
          displaydiag_readl(OMAP2420_DISPC_BASE + DISPC_SYSSTATUS));
   printf("  DISPC_CONFIG         : 0x%08x\n",
          displaydiag_readl(OMAP2420_DISPC_BASE + DISPC_CONFIG));
   v114_enable_rfbi_mode();
   printf("\n");

   printf("[4] RFBI soft reset and Nokia rfbi_init() baseline\n");
   displaydiag_writel(1U << 1, OMAP2420_RFBI_BASE + RFBI_SYSCONFIG);
   ret = v113_wait_rfbi_reset();
   if (ret) {
      printf("  ERROR: RFBI reset did not complete\n");
      return ret;
   }
   v113_rfbi_init();
   printf("  SYSSTATUS       : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_SYSSTATUS));
   printf("  CONFIG0         : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  CONTROL         : 0x%08x\n\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));

   printf("[5] RX-34 blizzard_power_up()\n");
   tahvo = cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE);
   if (tahvo < 0) {
      printf("  ERROR: Tahvo read failed: %d\n", tahvo);
      return tahvo;
   }
   printf("  Tahvo 0x07 before: 0x%04x\n", tahvo & 0xffff);
   ret = cbus_gpio_write_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE,
                             tahvo & ~TAHVO_VCORE_MASK);
   if (ret) {
      printf("  ERROR: Tahvo write failed: %d\n", ret);
      return ret;
   }
   mdelay(10);

   clksrc = displaydiag_readl(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   displaydiag_writel(clksrc & ~PRCM_OSC_DISABLE_MASK,
                      OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   printf("  Tahvo 0x07 active: 0x%04x\n",
          cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE) & 0xffff);
   printf("  osc_ck           : %lu Hz, enabled\n", osc_hz);
   printf("  POWERDOWN GPIO15 : OUT=%u IN=%u\n\n",
          !!(displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAOUT) &
             (1U << N800_BLIZZARD_PWRDN_BIT)),
          !!(displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAIN) &
             (1U << N800_BLIZZARD_PWRDN_BIT)));

   printf("[6] calc_extif_timings(osc_ck) exactly from blizzard.c/rfbi.c\n");
   ret = v113_calc_extif_timings(osc_hz, l4_hz / 1000UL,
                                  &reg_timing, &lut_timing, &extif_div);
   if (ret) {
      printf("  ERROR: initial RFBI timing calculation failed: %d\n", ret);
      return ret;
   }
   v113_log_timing("register timing", &reg_timing);
   v113_log_timing("LUT timing", &lut_timing);
   printf("  extif_mem_div   : %d\n", extif_div);
   v113_set_rfbi_timings(&reg_timing);
   printf("  programmed CONFIG0       : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  programmed ONOFF_TIME0   : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_ONOFF_TIME0));
   printf("  programmed CYCLE_TIME0   : 0x%08x\n\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CYCLE_TIME0));

   printf("[7] First mandatory Blizzard read: PLL_DIV\n");
   pll_div = v113_blizzard_read(BLIZZARD_PLL_DIV);
   printf("  PLL_DIV          : 0x%02x\n", pll_div);
   printf("  PLL locked       : %s\n\n", (pll_div & 0x80U) ? "yes" : "NO");
   if (!(pll_div & 0x80U)) {
      printf("  STOP: Nokia blizzard_setup_clocks() returns -ENODEV here.\n");
      printf("  No Blizzard register is written and no pixel transfer is attempted.\n");
      return -ENODEV;
   }

   printf("[8] Read Blizzard clock tree\n");
   clk_src = v113_blizzard_read(BLIZZARD_CLK_SRC);
   pll_div = v113_blizzard_read(BLIZZARD_PLL_DIV);
   synth0 = v113_blizzard_read(BLIZZARD_PLL_CLOCK_SYNTH_0);
   synth1 = v113_blizzard_read(BLIZZARD_PLL_CLOCK_SYNTH_1);
   pix_div = ((clk_src >> 3) & 0x1fU) + 1U;
   sys_div = 0;
   sys_mul = 0;
   if ((clk_src & (0x3U << 1)) == 0) {
      sys_div = (pll_div & 0x3fU) + 1U;
      sys_mul = synth0 | ((synth1 & 0x0fU) << 11);
      blizzard_sys_hz = osc_hz * sys_mul / sys_div;
   } else {
      blizzard_sys_hz = osc_hz;
   }
   pix_hz = blizzard_sys_hz / pix_div;
   printf("  CLK_SRC          : 0x%02x\n", clk_src);
   printf("  PLL_DIV          : 0x%02x\n", pll_div);
   printf("  PLL_SYNTH0       : 0x%02x\n", synth0);
   printf("  PLL_SYNTH1       : 0x%02x\n", synth1);
   printf("  sys_div/sys_mul  : %u / %u\n", sys_div, sys_mul);
   printf("  Blizzard SYSCLK  : %lu Hz\n", blizzard_sys_hz);
   printf("  Blizzard PIXCLK  : %lu Hz\n\n", pix_hz);

   printf("[9] Recalculate RFBI timings from Blizzard SYSCLK\n");
   ret = v113_calc_extif_timings(blizzard_sys_hz, l4_hz / 1000UL,
                                  &reg_timing, &lut_timing, &extif_div);
   if (ret) {
      printf("  ERROR: final RFBI timing calculation failed: %d\n", ret);
      return ret;
   }
   v113_log_timing("register timing", &reg_timing);
   v113_log_timing("LUT timing", &lut_timing);
   v113_set_rfbi_timings(&reg_timing);
   printf("\n");

   printf("[10] Identify Blizzard\n");
   rev = v113_blizzard_read(BLIZZARD_REV_CODE);
   conf = v113_blizzard_read(BLIZZARD_CONFIG);
   printf("  REV_CODE         : 0x%02x\n", rev);
   printf("  CONFIG           : 0x%02x\n", conf);
   if ((rev & 0xfcU) == 0xa4U)
      printf("  controller       : S1D13745\n\n");
   else if ((rev & 0xfcU) == 0x9cU)
      printf("  controller       : S1D13744\n\n");
   else {
      printf("  controller       : NOT RECOGNIZED\n");
      printf("  STOP: no pixel transfer with unidentified controller.\n");
      return -ENODEV;
   }

   printf("[11] Nokia set_window_regs() validation window\n");
   printf("  x=%u y=%u width=%u height=%u RGB565=0x%04x\n",
          V113_TEST_X, V113_TEST_Y, V113_TEST_WIDTH, V113_TEST_HEIGHT,
          V113_TEST_RGB565);
   v113_set_window(V113_TEST_X, V113_TEST_Y,
                   V113_TEST_WIDTH, V113_TEST_HEIGHT);
   v113_set_bits_per_cycle(16);
   printf("  window descriptor complete; RFBI switched to 16-bit\n\n");

   printf("[12] Pixel transfer\n");
   printf("  Validation uses Nokia rfbi_write_data() semantics: 16-bit RGB565\n");
   printf("  words written to RFBI_PARAM after set_window_regs().\n");
   for (i = 0; i < V113_TEST_WIDTH * V113_TEST_HEIGHT; i++)
      displaydiag_writel(V113_TEST_RGB565,
                         OMAP2420_RFBI_BASE + RFBI_PARAM);
   printf("  transferred       : %u pixels / %u bytes\n",
          V113_TEST_WIDTH * V113_TEST_HEIGHT,
          V113_TEST_WIDTH * V113_TEST_HEIGHT * 2U);
   printf("  expected result   : red 100x60 rectangle\n\n");

   printf("V1.14 SUCCESS PATH COMPLETE\n");
   printf("  REV_CODE          : 0x%02x\n", rev);
   printf("  final CONFIG0     : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  final CONTROL     : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));

   return 0;
}

static int displaydiag_cmd_osc_ck_rev_test(void)
{
   u32 clksrc_before;
   u32 clksrc_enabled;
   u32 clksrc_after;
   u8 rev;
   u8 conf;
   u8 pll_div;

   /*
    * RX-34 osso71 arch/arm/mach-omap2/clock.c:
    *
    *   static void omap2_set_osc_ck(int enable)
    *   {
    *      if (enable)
    *         PRCM_CLKSRC_CTRL &= ~(0x3 << 3);
    *      else
    *         PRCM_CLKSRC_CTRL |= 0x3 << 3;
    *   }
    *
    * PRCM_CLKSRC_CTRL is OMAP24XX_PRCM_BASE + 0x060.
    */
   clksrc_before = displaydiag_readl(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);

   printf("N800 osc_ck + Blizzard revision test\n");
   printf("===================================\n");
   printf("RX-34 osso71 source-derived OMAP2420 oscillator test.\n");
   printf("No Tahvo, GPIO, RFBI timing or Blizzard register is written.\n\n");

   printf("PRCM_CLKSRC_CTRL:\n");
   printf("  address              : 0x%08x\n",
          OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   printf("  before               : 0x%08x\n", clksrc_before);
   printf("  bits [4:3]           : 0x%x\n",
          (clksrc_before & PRCM_OSC_DISABLE_MASK) >> 3);
   printf("  osso71 interpretation: %s\n\n",
          (clksrc_before & PRCM_OSC_DISABLE_MASK) ?
          "osc_ck disable request present" :
          "osc_ck enabled");

   clksrc_enabled = clksrc_before & ~PRCM_OSC_DISABLE_MASK;
   displaydiag_writel(clksrc_enabled,
                      OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   mdelay(1);

   clksrc_enabled = displaydiag_readl(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);

   printf("After source-derived clk_enable(osc_ck):\n");
   printf("  PRCM_CLKSRC_CTRL     : 0x%08x\n", clksrc_enabled);
   printf("  bits [4:3]           : 0x%x\n\n",
          (clksrc_enabled & PRCM_OSC_DISABLE_MASK) >> 3);

   rev = displaydiag_blizzard_read8(BLIZZARD_REV_CODE);
   conf = displaydiag_blizzard_read8(BLIZZARD_CONFIG);
   pll_div = displaydiag_blizzard_read8(BLIZZARD_PLL_DIV);

   printf("Blizzard reads with osc_ck enabled:\n");
   printf("  REV_CODE  [0x00]     : 0x%02x\n", rev);
   printf("  CONFIG    [0x02]     : 0x%02x\n", conf);
   printf("  PLL_DIV   [0x04]     : 0x%02x\n", pll_div);
   printf("  revision class       : ");
   if ((rev & 0xfcU) == 0xa4U)
      printf("S1D13745 detected\n");
   else if ((rev & 0xfcU) == 0x9cU)
      printf("S1D13744 detected\n");
   else
      printf("not recognized\n");

   displaydiag_writel(clksrc_before,
                      OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);
   clksrc_after = displaydiag_readl(OMAP2420_PRCM_BASE + PRCM_CLKSRC_CTRL);

   printf("\nAfter test:\n");
   printf("  PRCM_CLKSRC_CTRL     : 0x%08x\n", clksrc_after);
   printf("  restore              : %s\n",
          clksrc_after == clksrc_before ? "OK" : "MISMATCH");
   printf("  RFBI CONFIG0         : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));

   return 0;
}

static int displaydiag_cmd_nokia_power_rev_test(void)
{
   int tahvo_before;
   int tahvo_active;
   int tahvo_after;
   int ret;
   u32 oe;
   u32 datain;
   u32 dataout;
   u8 rev;
   u8 conf;
   u8 pll_div;

   printf("N800 Nokia power + Blizzard revision test\n");
   printf("=========================================\n");
   printf("This test follows the RX-34 osso71 power prerequisite that can be\n");
   printf("reproduced safely here, then performs read-only Blizzard accesses.\n\n");

   tahvo_before = cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE);
   if (tahvo_before < 0) {
      printf("ERROR: Tahvo register 0x07 read failed: %d\n", tahvo_before);
      return tahvo_before;
   }

   oe = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_OE);
   datain = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAIN);
   dataout = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAOUT);

   printf("Before test:\n");
   printf("  Tahvo reg 0x07       : 0x%04x\n", tahvo_before & 0xffff);
   printf("  Tahvo Vcore nibble   : 0x%x\n", tahvo_before & TAHVO_VCORE_MASK);
   printf("  GPIO15 direction     : %s\n",
          (oe & BIT(N800_BLIZZARD_PWRDN_BIT)) ? "input" : "output");
   printf("  GPIO15 DATAIN        : %u\n",
          !!(datain & BIT(N800_BLIZZARD_PWRDN_BIT)));
   printf("  GPIO15 DATAOUT       : %u\n",
          !!(dataout & BIT(N800_BLIZZARD_PWRDN_BIT)));
   printf("  RFBI CONFIG0         : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  RFBI CONTROL         : 0x%08x\n\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));

   /*
    * RX-34 osso71 blizzard_power_up():
    *
    *   tahvo_set_clear_reg_bits(0x07, 0, 0xf);
    *   msleep(10);
    *   clk_enable(osc_ck);
    *   POWERDOWN = 1;
    *
    * Barebox is running continuously and does not enter the Linux OMAP2
    * retention path that releases osc_ck. The current N800 build also keeps
    * active peripherals running. Therefore this diagnostic does not invent a
    * PRCM write for osc_ck: it reproduces the explicit Tahvo operation and
    * verifies the already-established POWERDOWN state.
    */
   tahvo_active = tahvo_before & ~TAHVO_VCORE_MASK;
   ret = cbus_gpio_write_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE, tahvo_active);
   if (ret < 0) {
      printf("ERROR: Tahvo register 0x07 write failed: %d\n", ret);
      return ret;
   }

   mdelay(10);

   tahvo_active = cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE);
   if (tahvo_active < 0) {
      printf("ERROR: Tahvo register 0x07 verification read failed: %d\n",
             tahvo_active);
      return tahvo_active;
   }

   printf("Nokia power prerequisite active:\n");
   printf("  Tahvo reg 0x07       : 0x%04x\n", tahvo_active & 0xffff);
   printf("  Tahvo Vcore nibble   : 0x%x (osso71 clears bits 3:0)\n",
          tahvo_active & TAHVO_VCORE_MASK);
   printf("  settle delay         : 10 ms\n");
   printf("  osc_ck               : no guessed PRCM write; Barebox remains active\n");
   printf("  POWERDOWN            : GPIO15 already output/high\n\n");

   rev = displaydiag_blizzard_read8(BLIZZARD_REV_CODE);
   conf = displaydiag_blizzard_read8(BLIZZARD_CONFIG);
   pll_div = displaydiag_blizzard_read8(BLIZZARD_PLL_DIV);

   printf("Blizzard reads:\n");
   printf("  REV_CODE  [0x00]     : 0x%02x\n", rev);
   printf("  CONFIG    [0x02]     : 0x%02x\n", conf);
   printf("  PLL_DIV   [0x04]     : 0x%02x\n", pll_div);
   printf("  revision class       : ");
   if ((rev & 0xfcU) == 0xa4U)
      printf("S1D13745 detected\n");
   else if ((rev & 0xfcU) == 0x9cU)
      printf("S1D13744 detected\n");
   else
      printf("not recognized\n");

   ret = cbus_gpio_write_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE,
                             tahvo_before & 0xffffU);
   if (ret < 0) {
      printf("\nWARNING: failed to restore Tahvo register 0x07: %d\n", ret);
      return ret;
   }

   tahvo_after = cbus_gpio_read_reg(TAHVO_CBUS_ID, TAHVO_REG_VCORE);
   printf("\nAfter test:\n");
   if (tahvo_after < 0) {
      printf("  Tahvo restore read failed: %d\n", tahvo_after);
      return tahvo_after;
   }

   printf("  Tahvo reg 0x07       : 0x%04x\n", tahvo_after & 0xffff);
   printf("  restore              : %s\n",
          ((tahvo_after & 0xffff) == (tahvo_before & 0xffff)) ? "OK" : "MISMATCH");
   printf("  RFBI CONFIG0         : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));

   return 0;
}

static void displaydiag_rfbi_set_parallel_mode(u32 mode)
{
   u32 config0;

   config0 = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   config0 &= ~RFBI_PARALLEL_MODE_MASK;
   config0 |= mode;
   displaydiag_writel(config0, OMAP2420_RFBI_BASE + RFBI_CONFIG0);
}

static void displaydiag_blizzard_write_data8(u8 value)
{
   displaydiag_writel(value, OMAP2420_RFBI_BASE + RFBI_PARAM);
}

static void displaydiag_blizzard_set_window(u16 x, u16 y, u16 width, u16 height)
{
   u16 x_end = x + width - 1U;
   u16 y_end = y + height - 1U;
   u8 data[18];
   size_t i;

   data[0] = x;
   data[1] = x >> 8;
   data[2] = y;
   data[3] = y >> 8;
   data[4] = x_end;
   data[5] = x_end >> 8;
   data[6] = y_end;
   data[7] = y_end >> 8;

   data[8] = x;
   data[9] = x >> 8;
   data[10] = y;
   data[11] = y >> 8;
   data[12] = x_end;
   data[13] = x_end >> 8;
   data[14] = y_end;
   data[15] = y_end >> 8;

   data[16] = BLIZZARD_COLOR_RGB565;
   data[17] = BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE;

   displaydiag_rfbi_set_parallel_mode(RFBI_PARALLEL_MODE_8);
   displaydiag_writel(BLIZZARD_INPUT_WIN_X_START_0,
                      OMAP2420_RFBI_BASE + RFBI_CMD);

   for (i = 0; i < ARRAY_SIZE(data); i++)
      displaydiag_blizzard_write_data8(data[i]);
}

static int displaydiag_cmd_rectangle_test(void)
{
   u32 config0_before;
   u32 control_before;
   u32 pixel_count;
   u32 i;

   config0_before = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0);
   control_before = displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL);
   pixel_count = N800_RECT_WIDTH * N800_RECT_HEIGHT;

   printf("N800 Blizzard rectangle test\n");
   printf("============================\n");
   printf("WARNING: This command intentionally writes Blizzard registers and pixels.\n");
   printf("It should replace a 100x60 area of the inherited NOLO image with red.\n\n");

   printf("Source: Nokia RX-34 osso71 Blizzard sequence\n");
   printf("  window : x=%u y=%u width=%u height=%u\n",
          N800_RECT_X, N800_RECT_Y, N800_RECT_WIDTH, N800_RECT_HEIGHT);
   printf("  color  : RGB565 0x%04x\n", N800_RECT_COLOR_RGB565);
   printf("  pixels : %u\n", pixel_count);
   printf("  CONFIG0 before : 0x%08x\n", config0_before);
   printf("  CONTROL before : 0x%08x\n\n", control_before);

   /*
    * Nokia's set_window_regs() writes 18 consecutive bytes beginning at
    * BLIZZARD_INPUT_WIN_X_START_0 in 8-bit RFBI mode. The historical driver
    * explicitly notes that this leaves the Blizzard register index at the
    * display-memory data port.
    */
   displaydiag_blizzard_set_window(N800_RECT_X, N800_RECT_Y,
                                   N800_RECT_WIDTH, N800_RECT_HEIGHT);

   /*
    * Pixel data is RGB565. Match Nokia's RFBI path by switching back to
    * 16-bit cycles before sending the pixels.
    */
   displaydiag_rfbi_set_parallel_mode(RFBI_PARALLEL_MODE_16);

   for (i = 0; i < pixel_count; i++)
      displaydiag_writel(N800_RECT_COLOR_RGB565,
                         OMAP2420_RFBI_BASE + RFBI_PARAM);

   /*
    * Restore the complete inherited RFBI configuration. CONTROL and timing
    * registers are deliberately left untouched by this experiment.
    */
   displaydiag_writel(config0_before, OMAP2420_RFBI_BASE + RFBI_CONFIG0);

   printf("Rectangle transaction completed.\n");
   printf("  CONFIG0 restored : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  CONTROL after    : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));
   printf("\nIf no red rectangle is visible, do not repeat with a larger area.\n");
   printf("The next step is to verify the Nokia power/clock prerequisites from osso71.\n");

   return 0;
}

static void displaydiag_print_gpio_line(const char *name, u32 bit, u32 oe,
                                        u32 datain, u32 dataout)
{
   u32 mask = 1U << bit;

   printf("  %-10s GPIO%-2u mask=0x%08x direction=%-6s DATAIN=%u DATAOUT=%u\n",
          name, bit, mask, (oe & mask) ? "input" : "output",
          !!(datain & mask), !!(dataout & mask));
}

static int displaydiag_cmd_display_lines(void)
{
   u32 oe;
   u32 datain;
   u32 dataout;

   oe = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_OE);
   datain = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAIN);
   dataout = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAOUT);

   printf("N800 display control-line diagnostic\n");
   printf("====================================\n");
   printf("Read-only. No GPIO, padconf, RFBI or Blizzard register is modified.\n\n");

   printf("RX-34 schematic trace:\n");
   printf("  POWERDOWN -> OMAP2420 AD7 / vlynq_rx0 / GPIO15 -> S1D13745 POWERDOWN\n");
   printf("  LCD_RST   -> OMAP2420 U3 / gpmc_nbe1 / GPIO30 -> S1D13745 /RESET\n\n");

   printf("GPIO1 registers:\n");
   printf("  OE      @ 0x%08x = 0x%08x\n",
          OMAP2420_GPIO1_BASE + OMAP_GPIO_OE, oe);
   printf("  DATAIN  @ 0x%08x = 0x%08x\n",
          OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAIN, datain);
   printf("  DATAOUT @ 0x%08x = 0x%08x\n",
          OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAOUT, dataout);

   printf("\nDecoded display control lines:\n");
   displaydiag_print_gpio_line("POWERDOWN", N800_BLIZZARD_PWRDN_BIT,
                               oe, datain, dataout);
   displaydiag_print_gpio_line("LCD_RST", N800_BLIZZARD_RESET_BIT,
                               oe, datain, dataout);

   printf("\nElectrical notes from the RX-34 display schematic:\n");
   printf("  LCD_RST has a 10 kOhm pull-up to VIO_APE (1.8 V) and drives the\n");
   printf("  S1D13745 active-low /RESET input through a 150 Ohm series resistor.\n");
   printf("  The exact OMAP2420 padconf offset for U3/gpmc_nbe1 is not named by\n");
   printf("  the supplied .orig kernel tree, so this command deliberately does\n");
   printf("  not guess or modify that padconf register.\n");

   return 0;
}

static int displaydiag_cmd_board_path(void)
{
   u32 oe;
   u32 datain;
   u32 dataout;
   u32 mask = 1U << N800_BLIZZARD_PWRDN_BIT;

   oe = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_OE);
   datain = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAIN);
   dataout = displaydiag_readl(OMAP2420_GPIO1_BASE + OMAP_GPIO_DATAOUT);

   printf("N800 display board-path diagnostic\n");
   printf("==================================\n");
   printf("Read-only. No GPIO, RFBI, padconf or Blizzard register is modified.\n\n");

   printf("RFBI state:\n");
   printf("  CONTROL : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONTROL));
   printf("  CONFIG0 : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_CONFIG0));
   printf("  STATUS  : 0x%08x\n",
          displaydiag_readl(OMAP2420_RFBI_BASE + RFBI_STATUS));

   printf("\nN800 Blizzard power-down GPIO:\n");
   printf("  GPIO1 base       : 0x%08x\n", OMAP2420_GPIO1_BASE);
   printf("  GPIO15 mask      : 0x%08x\n", mask);
   printf("  OE               : 0x%08x\n", oe);
   printf("  DATAIN           : 0x%08x\n", datain);
   printf("  DATAOUT          : 0x%08x\n", dataout);
   printf("  GPIO15 direction : %s\n", (oe & mask) ? "input" : "output");
   printf("  GPIO15 DATAIN    : %u\n", !!(datain & mask));
   printf("  GPIO15 DATAOUT   : %u\n", !!(dataout & mask));

   printf("\nNote: historical N800/QEMU board definitions identify GPIO15 as\n");
   printf("the Blizzard power-down control. This command intentionally does not\n");
   printf("interpret active polarity; it only reports the electrical GPIO state.\n");

   return 0;
}

static int do_displaydiag(int argc, char *argv[])
{
   if (argc == 2 && !strcmp(argv[1], "nokia-init-v114"))
      return displaydiag_cmd_nokia_init_v114();

   if (argc == 2 && !strcmp(argv[1], "osc-ck-rev-test"))
      return displaydiag_cmd_osc_ck_rev_test();

   if (argc == 2 && !strcmp(argv[1], "nokia-power-rev-test"))
      return displaydiag_cmd_nokia_power_rev_test();

   if (argc == 2 && !strcmp(argv[1], "rectangle-test"))
      return displaydiag_cmd_rectangle_test();

   if (argc == 2 && !strcmp(argv[1], "display-lines"))
      return displaydiag_cmd_display_lines();


   if (argc == 2 && !strcmp(argv[1], "board-path"))
      return displaydiag_cmd_board_path();

   if (argc == 2 && !strcmp(argv[1], "pinmux"))
      return displaydiag_cmd_pinmux();


   if (argc < 2)
      return COMMAND_ERROR_USAGE;

   if (!strcmp(argv[1], "status")) {
      printf("N800 display diagnostics (read-mostly snapshot)\n");
      printf("==============================================\n\n");
      displaydiag_dump_clocks();
      printf("\n");
      displaydiag_dump_block(&blocks[0]);
      printf("\n");
      displaydiag_dump_block(&blocks[1]);
      printf("\n");
      displaydiag_dump_block(&blocks[2]);
      return 0;
   }

   if (!strcmp(argv[1], "clocks")) {
      displaydiag_dump_clocks();
      return 0;
   }

   if (!strcmp(argv[1], "dss")) {
      displaydiag_dump_block(&blocks[0]);
      return 0;
   }

   if (!strcmp(argv[1], "dispc")) {
      displaydiag_dump_block(&blocks[1]);
      return 0;
   }

   if (!strcmp(argv[1], "rfbi")) {
      displaydiag_dump_block(&blocks[2]);
      return 0;
   }

   if (!strcmp(argv[1], "blizzard")) {
      displaydiag_dump_blizzard();
      return 0;
   }

   if (!strcmp(argv[1], "blizzard-read-debug"))
      return displaydiag_cmd_blizzard_read_debug(argc, argv);

   if (!strcmp(argv[1], "blizzard-bus-debug"))
      return displaydiag_cmd_blizzard_bus_debug(argc, argv);

   if (!strcmp(argv[1], "blizzard-historical-read-test"))
      return displaydiag_cmd_blizzard_historical_read_test(argc, argv);

   if (!strcmp(argv[1], "pixel-test"))
      return displaydiag_cmd_pixel_test(argc, argv);

   if (!strcmp(argv[1], "read"))
      return displaydiag_cmd_read(argc, argv);

   if (!strcmp(argv[1], "write"))
      return displaydiag_cmd_write(argc, argv);

   if (!strcmp(argv[1], "modify"))
      return displaydiag_cmd_modify(argc, argv);

   return COMMAND_ERROR_USAGE;
}

BAREBOX_CMD_HELP_START(displaydiag)
BAREBOX_CMD_HELP_TEXT("displaydiag nokia-init-v114 - RX-34 DISPC/RFBI init, clock/timing calculation and pixel validation")
BAREBOX_CMD_HELP_TEXT("displaydiag osc-ck-rev-test - enable osc_ck exactly as RX-34 osso71 and read Blizzard ID")
BAREBOX_CMD_HELP_TEXT("displaydiag nokia-power-rev-test - apply Nokia Tahvo Vcore prerequisite and read Blizzard ID")
BAREBOX_CMD_HELP_TEXT("displaydiag rectangle-test - draw a 100x60 red RGB565 test rectangle")
BAREBOX_CMD_HELP_TEXT("displaydiag display-lines - read RX-34 Blizzard POWERDOWN and LCD_RST GPIO states")
BAREBOX_CMD_HELP_TEXT("displaydiag board-path - read RFBI and N800 Blizzard power-down GPIO state")
BAREBOX_CMD_HELP_TEXT("displaydiag pinmux - read source-verified OMAP2420 display padconf registers")
BAREBOX_CMD_HELP_TEXT("Nokia N800 OMAP2420 DSS/RFBI/Blizzard bring-up diagnostics")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Read-only diagnostics:")
BAREBOX_CMD_HELP_TEXT("  displaydiag status")
BAREBOX_CMD_HELP_TEXT("  displaydiag clocks")
BAREBOX_CMD_HELP_TEXT("  displaydiag dss")
BAREBOX_CMD_HELP_TEXT("  displaydiag dispc")
BAREBOX_CMD_HELP_TEXT("  displaydiag rfbi")
BAREBOX_CMD_HELP_TEXT("  displaydiag blizzard")
BAREBOX_CMD_HELP_TEXT("  displaydiag blizzard-read-debug <reg>")
BAREBOX_CMD_HELP_TEXT("  displaydiag blizzard-bus-debug")
BAREBOX_CMD_HELP_TEXT("  displaydiag blizzard-historical-read-test")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Inherited-state pixel test:")
BAREBOX_CMD_HELP_TEXT("  displaydiag pixel-test <rgb565> <count>")
BAREBOX_CMD_HELP_TEXT("  count range: 1..384000")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("Register access:")
BAREBOX_CMD_HELP_TEXT("  displaydiag read <dss|dispc|rfbi> <name|offset>")
BAREBOX_CMD_HELP_TEXT("  displaydiag read blizzard <reg>")
BAREBOX_CMD_HELP_TEXT("  displaydiag write <dss|dispc|rfbi> <name|offset> <value>")
BAREBOX_CMD_HELP_TEXT("  displaydiag write blizzard <reg> <value>")
BAREBOX_CMD_HELP_TEXT("  displaydiag modify <dss|dispc|rfbi> <name|offset> <clear> <set>")
BAREBOX_CMD_HELP_TEXT("  displaydiag modify blizzard <reg> <clear> <set>")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("WARNING: pixel-test, write and modify alter the active hardware state.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(displaydiag)
   .cmd = do_displaydiag,
   BAREBOX_CMD_DESC("N800 display hardware diagnostics")
   BAREBOX_CMD_OPTS("<command> [arguments]")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
   BAREBOX_CMD_HELP(cmd_displaydiag_help)
BAREBOX_CMD_END
