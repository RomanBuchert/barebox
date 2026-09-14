// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <dma.h>
#include <fb.h>
#include <init.h>
#include <linux/string.h>

#include "omap2-rfbi.h"

#define N800_LCD_WIDTH  800
#define N800_LCD_HEIGHT 480
#define N800_LCD_BPP    16

#define RGB565_RED   0xf800
#define RGB565_GREEN 0x07e0
#define RGB565_BLUE  0x001f

struct n800_blizzard {
   struct fb_info info;
   struct fb_videomode mode;
   dma_addr_t framebuffer_phys;
};

static struct n800_blizzard n800_display;

static void blizzard_write_reg(u8 reg, u8 value)
{
   omap2_rfbi_write_command(reg);
   omap2_rfbi_write_parameter(value);
}

static void blizzard_setup_window(void)
{
   /* LCD geometry. Width is stored in units of eight pixels. */
   blizzard_write_reg(0x2a, 0x64);
   blizzard_write_reg(0x2c, 0x1e);
   blizzard_write_reg(0x2e, 0xe0);
   blizzard_write_reg(0x30, 0x01);
   blizzard_write_reg(0x32, 0x06);

   /* Enable display output. */
   blizzard_write_reg(0x68, 0x01);

   /* Input and output windows: x=0..799, y=0..479. */
   omap2_rfbi_write_command(0x6c);

   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x1f);
   omap2_rfbi_write_parameter(0x03);
   omap2_rfbi_write_parameter(0xdf);
   omap2_rfbi_write_parameter(0x01);

   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x00);
   omap2_rfbi_write_parameter(0x1f);
   omap2_rfbi_write_parameter(0x03);
   omap2_rfbi_write_parameter(0xdf);
   omap2_rfbi_write_parameter(0x01);

   /* RGB565 input and display-memory data source. */
   omap2_rfbi_write_parameter(0x01);
   omap2_rfbi_write_parameter(0x01);
}

static void n800_blizzard_transfer(struct n800_blizzard *display)
{
   struct fb_info *info = &display->info;

   if (info->screen_base_shadow)
      memcpy(info->screen_base, info->screen_base_shadow, info->screen_size);

   omap2_rfbi_transfer(display->framebuffer_phys, info->xres, info->yres);
}

static void n800_blizzard_enable(struct fb_info *info)
{
   struct n800_blizzard *display = info->priv;

   omap2_rfbi_init();
   omap2_rfbi_select(OMAP2_RFBI_CS0);
   blizzard_setup_window();
   n800_blizzard_transfer(display);
}

static void n800_blizzard_flush(struct fb_info *info)
{
   struct n800_blizzard *display = info->priv;

   n800_blizzard_transfer(display);
}

static struct fb_ops n800_blizzard_ops = {
   .fb_enable = n800_blizzard_enable,
   .fb_flush = n800_blizzard_flush,
};

static void n800_fill_rgb_test_pattern(struct fb_info *info)
{
   u16 *fb = info->screen_base;
   unsigned int x;
   unsigned int y;

   for (y = 0; y < info->yres; y++) {
      for (x = 0; x < info->xres; x++) {
         if (x < 267)
            fb[y * info->xres + x] = RGB565_RED;
         else if (x < 533)
            fb[y * info->xres + x] = RGB565_GREEN;
         else
            fb[y * info->xres + x] = RGB565_BLUE;
      }
   }
}

static int n800_blizzard_init(void)
{
   struct n800_blizzard *display = &n800_display;
   struct fb_info *info = &display->info;
   size_t size;
   int ret;

   memset(display, 0, sizeof(*display));

   display->mode.name = "800x480";
   display->mode.xres = N800_LCD_WIDTH;
   display->mode.yres = N800_LCD_HEIGHT;

   info->mode = &display->mode;
   info->xres = N800_LCD_WIDTH;
   info->yres = N800_LCD_HEIGHT;
   info->bits_per_pixel = N800_LCD_BPP;
   info->line_length = N800_LCD_WIDTH * sizeof(u16);
   info->screen_size = info->line_length * N800_LCD_HEIGHT;
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

   n800_fill_rgb_test_pattern(info);

   ret = register_framebuffer(info);
   if (ret)
      return ret;

   /*
    * Enable immediately so the framebuffer itself is the first visible
    * Barebox output. Later the shell/menu can use the registered fb0.
    */
   return fb_enable(info);
}
device_initcall(n800_blizzard_init);
