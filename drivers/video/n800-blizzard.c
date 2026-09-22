// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <clock.h>
#include <dma.h>
#include <fb.h>
#include <init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <poller.h>

#include "n800-blizzard-core.h"
#include "n800-display-power.h"
#include "omap2-dispc.h"
#include "omap2-rfbi.h"

struct n800_blizzard {
   struct fb_info info;
   struct fb_videomode mode;
   struct n800_blizzard_bus bus;
   struct n800_blizzard_info controller;
   dma_addr_t framebuffer_phys;
   bool transfer_error_reported;
   bool transfer_busy;
   bool transfer_disabled;
   unsigned long write_cycle_ps;
};

static struct n800_blizzard n800_display;

static void bus_set_bits(void *context, unsigned int bits)
{
   omap2_rfbi_set_bits_per_cycle(bits);
}

static u8 bus_read_reg(void *context, u8 reg)
{
   return omap2_rfbi_read_reg8(reg);
}

static void bus_write_command(void *context, u8 command)
{
   omap2_rfbi_write_command8(command);
}

static void bus_write_data8(void *context, u8 value)
{
   omap2_rfbi_write_data8(value);
}

static int bus_setup_tearsync(void *context, unsigned int pin_count,
                              unsigned int hs_pulse_ps, unsigned int vs_pulse_ps,
                              bool hs_pol_inv, bool vs_pol_inv)
{
   return omap2_rfbi_setup_tearsync(pin_count, hs_pulse_ps, vs_pulse_ps,
                                    hs_pol_inv, vs_pol_inv);
}

static unsigned long bus_get_max_tx_rate(void *context)
{
   return omap2_rfbi_get_max_tx_rate();
}

static int n800_blizzard_prepare(struct n800_blizzard *display)
{
   int ret;

   pr_info("n800-display: hardware initialization begin\n");

   ret = omap2_rfbi_init();
   if (ret)
      return ret;
   pr_info("n800-display: RFBI initialized\n");

   ret = n800_display_power_up();
   if (ret)
      return ret;
   pr_info("n800-display: display power enabled\n");

   ret = omap2_rfbi_set_timings(omap2_rfbi_get_osc_rate(), NULL);
   if (ret)
      return ret;

   ret = n800_blizzard_probe(&display->bus, omap2_rfbi_get_osc_rate(),
                             &display->controller);
   if (ret)
      return ret;
   pr_info("n800-display: Blizzard controller detected, revision 0x%02x\n",
           display->controller.revision);

   ret = omap2_rfbi_set_timings(display->controller.sys_hz,
                                &display->write_cycle_ps);
   if (ret)
      return ret;

   /* N800 board-n800.c sets te_connected = 1.  RX-34 blizzard_setup_clocks()
    * therefore always executes setup_tearsync() after the final RFBI timings.
    */
   ret = n800_blizzard_setup_tearsync(&display->bus, &display->controller,
                                      display->write_cycle_ps);
   if (ret)
      return ret;
   pr_info("n800-display: tear sync configured\n");

   pr_info("n800-blizzard: S1D1374%c rev=0x%02x sys=%lu Hz pixel=%lu Hz\n",
           (display->controller.revision & 0xfcU) == 0xa4U ? '5' : '4',
           display->controller.revision, display->controller.sys_hz,
           display->controller.pixel_hz);
   return 0;
}

static void n800_blizzard_report_transfer_error(struct n800_blizzard *display, int ret)
{
   if (display->transfer_error_reported)
      return;

   display->transfer_error_reported = true;
   pr_err("n800-blizzard: DISPC/RFBI transfer failed: %pe\n", ERR_PTR(ret));
}

static void n800_blizzard_sync_shadow_rect(struct fb_info *info, unsigned int x,
                                             unsigned int y, unsigned int width,
                                             unsigned int height)
{
   unsigned int row;
   size_t offset;
   size_t bytes = width * sizeof(u16);

   if (!info->screen_base_shadow)
      return;

   for (row = 0; row < height; row++) {
      offset = (y + row) * info->line_length + x * sizeof(u16);
      memcpy((u8 *)info->screen_base + offset,
             (u8 *)info->screen_base_shadow + offset, bytes);
   }
}

static void n800_blizzard_transfer_rect(struct n800_blizzard *display,
                                        unsigned int x, unsigned int y,
                                        unsigned int width, unsigned int height)
{
   struct fb_info *info = &display->info;
   int ret;

   if (!width || !height || display->transfer_busy || display->transfer_disabled)
      return;

   display->transfer_busy = true;

   n800_blizzard_sync_shadow_rect(info, x, y, width, height);

   /*
    * Single-plane RGB565 equivalent of Nokia blizzard.c do_partial_update():
    * setup the overlapping GFX source area, enable the plane, program the
    * Blizzard input/output window, select 16-bit cycles, transfer_area().
    */
   omap2_dispc_setup_plane(display->framebuffer_phys, info->xres,
                           x, y, width, height);
   omap2_dispc_enable_plane(true);

   /*
    * Nokia RX-34 do_partial_update() ordering.  Do not program a new
    * Blizzard window until the previous line-buffer operation is complete.
    */
   n800_blizzard_wait_line_buffer(&display->bus);

   /* No OMAPFB_FORMAT_FLAG_TEARSYNC: exact RX-34 else branch. */
   omap2_rfbi_enable_tearsync(false, 0);
   n800_blizzard_disable_tearsync(&display->bus);

   n800_blizzard_set_window(&display->bus, &display->controller,
                             x, y, width, height);
   display->bus.set_bits_per_cycle(display->bus.context, 16);

   ret = omap2_rfbi_transfer(width, height);
   if (ret) {
      /* Stop repeated console damage from turning one failed transfer into a
       * permanent boot stall.  The Nokia hardware path remains untouched.
       */
      display->transfer_disabled = true;
      n800_blizzard_report_transfer_error(display, ret);
   }

   display->transfer_busy = false;
}

static void n800_blizzard_enable(struct fb_info *info)
{
   pr_info("n800-display: initial framebuffer transfer begin\n");
   n800_blizzard_transfer_rect(info->priv, 0, 0, info->xres, info->yres);
   pr_info("n800-display: initial framebuffer transfer complete\n");
}

static void n800_blizzard_damage(struct fb_info *info, const struct fb_rect *rect)
{
   struct n800_blizzard *display = info->priv;
   unsigned int width = fb_rect_width(rect);
   unsigned int height = fb_rect_height(rect);

   if (rect->x1 >= info->xres || rect->y1 >= info->yres)
      return;

   width = min(width, info->xres - rect->x1);
   height = min(height, info->yres - rect->y1);
   n800_blizzard_transfer_rect(display, rect->x1, rect->y1, width, height);
}

static struct fb_ops n800_blizzard_ops = {
   .fb_enable = n800_blizzard_enable,
   .fb_damage = n800_blizzard_damage,
};

static int n800_blizzard_init(void)
{
   struct n800_blizzard *display = &n800_display;
   struct fb_info *info = &display->info;
   uint64_t start;
   size_t size;
   int ret;

   /*
    * The TUSB6010/CDC gadget is registered at device_initcall level.  Give
    * the host a short diagnostic window to enumerate and open CDC before the
    * display touches any hardware.  poller_call() keeps USB and the Retu
    * watchdog alive during this temporary bring-up delay.
    */
   start = get_time_ns();
   while (!is_timeout(start, 2ULL * SECOND)) {
      poller_call();
      udelay(1000);
   }

   pr_info("n800-display: delayed initialization after USB CDC initcalls\n");

   memset(display, 0, sizeof(*display));

   display->bus.context = display;
   display->bus.set_bits_per_cycle = bus_set_bits;
   display->bus.read_reg = bus_read_reg;
   display->bus.write_command = bus_write_command;
   display->bus.write_data8 = bus_write_data8;
   display->bus.setup_tearsync = bus_setup_tearsync;
   display->bus.get_max_tx_rate = bus_get_max_tx_rate;

   ret = n800_blizzard_prepare(display);
   if (ret) {
      pr_err("n800-blizzard: hardware initialization failed: %pe\n", ERR_PTR(ret));
      return ret;
   }

   display->mode.name = "800x480";
   display->mode.xres = N800_BLIZZARD_WIDTH;
   display->mode.yres = N800_BLIZZARD_HEIGHT;

   info->mode = &display->mode;
   info->xres = N800_BLIZZARD_WIDTH;
   info->yres = N800_BLIZZARD_HEIGHT;
   info->bits_per_pixel = 16;
   info->line_length = N800_BLIZZARD_WIDTH * sizeof(u16);
   info->screen_size = info->line_length * N800_BLIZZARD_HEIGHT;
   info->red.offset = 11;
   info->red.length = 5;
   info->green.offset = 5;
   info->green.length = 6;
   info->blue.offset = 0;
   info->blue.length = 5;
   info->fbops = &n800_blizzard_ops;
   info->priv = display;

   size = info->screen_size;
   info->screen_base = dma_alloc_coherent(DMA_DEVICE_BROKEN, size,
                                           &display->framebuffer_phys);
   if (!info->screen_base)
      return -ENOMEM;
   memset(info->screen_base, 0, size);
   pr_info("n800-display: framebuffer memory allocated\n");

   pr_info("n800-display: registering framebuffer\n");
   ret = register_framebuffer(info);
   if (ret)
      return ret;
   pr_info("n800-display: framebuffer registered\n");

   pr_info("n800-display: enabling framebuffer\n");
   ret = fb_enable(info);
   if (ret)
      return ret;

   pr_info("n800-display: framebuffer enabled\n");
   return 0;
}

/*
 * Keep display bring-up after the N800 TUSB6010 device_initcall while the
 * hardware path is still under investigation.  This gives the CDC ACM
 * console a chance to come up before RFBI/DISPC/Blizzard initialization.
 * Move this back to the normal device level once the real-hardware path is
 * stable.
 */
postenvironment_initcall(n800_blizzard_init);
