// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <command.h>
#include <errno.h>
#include <fcntl.h>
#include <fs.h>
#include <linux/kernel.h>
#include <malloc.h>

#define N800_FB_PATH "/dev/fb0"
#define N800_FB_PIXELS (800U * 480U)

static int n800_fb_fd = -1;

static u16 n800_rgb888_to_rgb565(u32 rgb)
{
   u16 red = (rgb >> 19) & 0x1f;
   u16 green = (rgb >> 10) & 0x3f;
   u16 blue = (rgb >> 3) & 0x1f;

   return (red << 11) | (green << 5) | blue;
}

static int n800_fb_open(void)
{
   if (n800_fb_fd >= 0)
      return 0;

   n800_fb_fd = open(N800_FB_PATH, O_WRONLY);
   if (n800_fb_fd < 0) {
      printf("n800fbwrite: cannot open %s: %m\n", N800_FB_PATH);
      return n800_fb_fd;
   }

   printf("n800fbwrite: opened %s as fd %d; keeping it open to avoid close/flush\n",
          N800_FB_PATH, n800_fb_fd);
   return 0;
}

static int do_n800fbwrite(int argc, char *argv[])
{
   unsigned int pixels;
   size_t bytes;
   u32 rgb;
   u16 rgb565;
   u16 *buffer;
   ssize_t written;
   int ret;
   unsigned int i;

   if (argc == 2 && !strcmp(argv[1], "close")) {
      if (n800_fb_fd < 0) {
         printf("n800fbwrite: %s is not open\n", N800_FB_PATH);
         return 0;
      }

      printf("n800fbwrite: closing fd %d; this intentionally exercises fb_flush()\n",
             n800_fb_fd);
      ret = close(n800_fb_fd);
      n800_fb_fd = -1;
      return ret ? COMMAND_ERROR : 0;
   }

   if (argc != 3)
      return COMMAND_ERROR_USAGE;

   ret = kstrtou32(argv[1], 0, &rgb);
   if (ret || rgb > 0xffffff)
      return COMMAND_ERROR_USAGE;

   ret = kstrtouint(argv[2], 0, &pixels);
   if (ret || !pixels || pixels > N800_FB_PIXELS)
      return COMMAND_ERROR_USAGE;

   ret = n800_fb_open();
   if (ret)
      return COMMAND_ERROR;

   rgb565 = n800_rgb888_to_rgb565(rgb);
   bytes = (size_t)pixels * sizeof(*buffer);
   buffer = xmalloc(bytes);

   for (i = 0; i < pixels; i++)
      buffer[i] = rgb565;

   if (lseek(n800_fb_fd, 0, SEEK_SET) < 0) {
      printf("n800fbwrite: lseek failed: %m\n");
      free(buffer);
      return COMMAND_ERROR;
   }

   printf("n800fbwrite: write(%s): %u pixels, %zu bytes, RGB888 0x%06x -> RGB565 0x%04x\n",
          N800_FB_PATH, pixels, bytes, rgb, rgb565);

   written = write(n800_fb_fd, buffer, bytes);
   free(buffer);

   if (written < 0) {
      printf("n800fbwrite: write failed: %m\n");
      return COMMAND_ERROR;
   }

   printf("n800fbwrite: write returned %zd; no manual fb_damage(), fb_flush(), or close()\n",
          written);

   if (written != bytes) {
      printf("n800fbwrite: short write: expected %zu bytes\n", bytes);
      return COMMAND_ERROR;
   }

   return 0;
}

BAREBOX_CMD_START(n800fbwrite)
   .cmd = do_n800fbwrite,
   BAREBOX_CMD_DESC("test real /dev/fb0 write damage path on Nokia N800")
   BAREBOX_CMD_OPTS("COLOR PIXELS | close")
   BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
BAREBOX_CMD_END
