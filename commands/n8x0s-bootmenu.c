// SPDX-License-Identifier: GPL-2.0-only

#include <command.h>
#include <common.h>
#include <errno.h>
#include <fb.h>
#include <gui/graphic_utils.h>
#include <gui/image_renderer.h>

#define N8X0S_FBDEV "/dev/fb0"
#define N8X0S_ASSET_DIR "/env/n8x0s/bootmenu"
#define N8X0S_WIDTH 800
#define N8X0S_HEIGHT 480
#define N8X0S_ITEM_COUNT 4

struct n8x0s_bootmenu_item {
   const char *icon_file;
   const char *label_file;
   int tile_x;
};

struct n8x0s_bootmenu_images {
   struct image *background;
   struct image *tile;
   struct image *tile_selected;
   struct image *icons[N8X0S_ITEM_COUNT];
   struct image *labels[N8X0S_ITEM_COUNT];
};

static const struct n8x0s_bootmenu_item n8x0s_items[N8X0S_ITEM_COUNT] = {
   { "sdcard-1.png", "label-sd1.png", CONFIG_N8X0S_BOOTMENU_SD1_X },
   { "sdcard-2.png", "label-sd2.png", CONFIG_N8X0S_BOOTMENU_SD2_X },
   { "internal.png", "label-internal.png", CONFIG_N8X0S_BOOTMENU_INTERNAL_X },
   { "power.png", "label-power.png", CONFIG_N8X0S_BOOTMENU_POWER_X },
};

static struct image *n8x0s_open_asset(const char *name)
{
   char path[128];

   snprintf(path, sizeof(path), "%s/%s", N8X0S_ASSET_DIR, name);
   return image_renderer_open(path);
}

static void n8x0s_close_images(struct n8x0s_bootmenu_images *images)
{
   unsigned int i;

   for (i = 0; i < N8X0S_ITEM_COUNT; i++) {
      if (!IS_ERR_OR_NULL(images->icons[i]))
         image_renderer_close(images->icons[i]);
      if (!IS_ERR_OR_NULL(images->labels[i]))
         image_renderer_close(images->labels[i]);
   }

   if (!IS_ERR_OR_NULL(images->tile_selected))
      image_renderer_close(images->tile_selected);
   if (!IS_ERR_OR_NULL(images->tile))
      image_renderer_close(images->tile);
   if (!IS_ERR_OR_NULL(images->background))
      image_renderer_close(images->background);
}

static int n8x0s_load_images(struct n8x0s_bootmenu_images *images)
{
   unsigned int i;

   memset(images, 0, sizeof(*images));

   images->background = n8x0s_open_asset("background.png");
   if (IS_ERR(images->background))
      goto error;

   images->tile = n8x0s_open_asset("tile.png");
   if (IS_ERR(images->tile))
      goto error;

   images->tile_selected = n8x0s_open_asset("tile-selected.png");
   if (IS_ERR(images->tile_selected))
      goto error;

   for (i = 0; i < N8X0S_ITEM_COUNT; i++) {
      images->icons[i] = n8x0s_open_asset(n8x0s_items[i].icon_file);
      if (IS_ERR(images->icons[i]))
         goto error;

      images->labels[i] = n8x0s_open_asset(n8x0s_items[i].label_file);
      if (IS_ERR(images->labels[i]))
         goto error;
   }

   return 0;

error:
   n8x0s_close_images(images);
   return -ENOENT;
}

static int n8x0s_render_image(struct screen *sc, struct image *image, int x, int y)
{
   struct surface surface = {
      .x = x,
      .y = y,
      .width = -1,
      .height = -1,
   };

   return image_renderer_image(sc, &surface, image);
}

static int n8x0s_draw_item(struct screen *sc, struct n8x0s_bootmenu_images *images,
                           unsigned int item, bool selected)
{
   struct image *tile = selected ? images->tile_selected : images->tile;
   int center_x = n8x0s_items[item].tile_x + CONFIG_N8X0S_BOOTMENU_TILE_WIDTH / 2;
   int icon_x = center_x - images->icons[item]->width / 2;
   int icon_y = CONFIG_N8X0S_BOOTMENU_TILE_Y + CONFIG_N8X0S_BOOTMENU_ICON_Y_OFFSET -
                images->icons[item]->height / 2;
   int label_x = center_x - images->labels[item]->width / 2;
   int label_y = CONFIG_N8X0S_BOOTMENU_TILE_Y + CONFIG_N8X0S_BOOTMENU_LABEL_Y_OFFSET -
                 images->labels[item]->height / 2;
   int ret;

   ret = n8x0s_render_image(sc, tile, n8x0s_items[item].tile_x, CONFIG_N8X0S_BOOTMENU_TILE_Y);
   if (ret < 0)
      return ret;

   ret = n8x0s_render_image(sc, images->icons[item], icon_x, icon_y);
   if (ret < 0)
      return ret;

   return n8x0s_render_image(sc, images->labels[item], label_x, label_y);
}

static int n8x0s_draw_menu(struct screen *sc, struct n8x0s_bootmenu_images *images)
{
   unsigned int i;
   int ret;

   ret = n8x0s_render_image(sc, images->background, 0, 0);
   if (ret < 0)
      return ret;

   for (i = 0; i < N8X0S_ITEM_COUNT; i++) {
      ret = n8x0s_draw_item(sc, images, i, i == CONFIG_N8X0S_BOOTMENU_INITIAL_SELECTION);
      if (ret < 0)
         return ret;
   }

   /* One deliberate full-screen transfer for the initial static menu. */
   gu_screen_blit_area(sc, 0, 0, N8X0S_WIDTH, N8X0S_HEIGHT);

   return 0;
}

static int do_n8x0s_bootmenu(int argc, char *argv[])
{
   struct n8x0s_bootmenu_images images;
   struct screen *sc;
   int ret;

   sc = fb_open(N8X0S_FBDEV);
   if (IS_ERR(sc)) {
      printf("N8x0s: unable to open %s: %pe\n", N8X0S_FBDEV, sc);
      return COMMAND_ERROR;
   }

   if (sc->s.width != N8X0S_WIDTH || sc->s.height != N8X0S_HEIGHT) {
      printf("N8x0s: unsupported framebuffer geometry %dx%d\n", sc->s.width, sc->s.height);
      ret = COMMAND_ERROR;
      goto close_fb;
   }

   ret = n8x0s_load_images(&images);
   if (ret) {
      printf("N8x0s: unable to load boot menu assets from %s\n", N8X0S_ASSET_DIR);
      ret = COMMAND_ERROR;
      goto close_fb;
   }

   ret = n8x0s_draw_menu(sc, &images);
   if (ret < 0) {
      printf("N8x0s: unable to render boot menu: %pe\n", ERR_PTR(ret));
      ret = COMMAND_ERROR;
   } else {
      ret = 0;
   }

   n8x0s_close_images(&images);
close_fb:
   fb_close(sc);

   return ret;
}

BAREBOX_CMD_HELP_START(n8x0s_bootmenu)
BAREBOX_CMD_HELP_TEXT("Display the static N8x0s boot menu test screen.")
BAREBOX_CMD_HELP_TEXT("This version intentionally has no input handling or boot actions.")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(n8x0s_bootmenu)
   .cmd = do_n8x0s_bootmenu,
   BAREBOX_CMD_DESC("display the N8x0s graphical boot menu")
   BAREBOX_CMD_GROUP(CMD_GRP_BOOT)
   BAREBOX_CMD_HELP(cmd_n8x0s_bootmenu_help)
BAREBOX_CMD_END
