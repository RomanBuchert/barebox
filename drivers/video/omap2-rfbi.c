// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <dma.h>
#include <linux/io.h>

#include "omap2-rfbi.h"

#define OMAP2_DSS_BASE   0x48050000
#define OMAP2_DISPC_BASE 0x48050400
#define OMAP2_RFBI_BASE  0x48050800

#define DSS_SYSCONFIG 0x010
#define DSS_CONTROL   0x040

#define DISPC_SYSCONFIG     0x010
#define DISPC_CONTROL       0x040
#define DISPC_GFX_BA0       0x080
#define DISPC_GFX_SIZE      0x08c
#define DISPC_GFX_ROW_INC   0x0ac
#define DISPC_GFX_PIXEL_INC 0x0b0

#define RFBI_SYSCONFIG 0x010
#define RFBI_CONTROL   0x040
#define RFBI_PIXELCNT  0x044
#define RFBI_CMD       0x04c
#define RFBI_PARAM     0x050

#define DISPC_CONTROL_RFBIMODE BIT(11)

#define RFBI_CONTROL_ENABLE BIT(0)
#define RFBI_CONTROL_CS0    BIT(2)
#define RFBI_CONTROL_CS1    BIT(3)
#define RFBI_CONTROL_ITE    BIT(4)

static u32 rfbi_control;

void omap2_rfbi_init(void)
{
   /*
    * Normalize the DSS/RFBI state used by the N800 display path. SDRAM is
    * intentionally not touched here; it belongs to the NOLO/QEMU handoff.
    */
   writel(0, (void __iomem *)(OMAP2_DSS_BASE + DSS_SYSCONFIG));
   writel(0, (void __iomem *)(OMAP2_DSS_BASE + DSS_CONTROL));

   writel(0, (void __iomem *)(OMAP2_DISPC_BASE + DISPC_SYSCONFIG));
   writel(1, (void __iomem *)(OMAP2_DISPC_BASE + DISPC_GFX_ROW_INC));
   writel(1, (void __iomem *)(OMAP2_DISPC_BASE + DISPC_GFX_PIXEL_INC));
   writel(DISPC_CONTROL_RFBIMODE,
          (void __iomem *)(OMAP2_DISPC_BASE + DISPC_CONTROL));

   writel(0, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_SYSCONFIG));

   rfbi_control = RFBI_CONTROL_ENABLE | RFBI_CONTROL_CS0;
   writel(rfbi_control, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_CONTROL));
}

void omap2_rfbi_select(unsigned int chip_select)
{
   rfbi_control &= ~(RFBI_CONTROL_CS0 | RFBI_CONTROL_CS1);

   if (chip_select == 0)
      rfbi_control |= RFBI_CONTROL_CS0;
   else
      rfbi_control |= RFBI_CONTROL_CS1;

   writel(rfbi_control, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_CONTROL));
}

void omap2_rfbi_write_command(u16 value)
{
   writel(value, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_CMD));
}

void omap2_rfbi_write_parameter(u16 value)
{
   writel(value, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_PARAM));
}

void omap2_rfbi_transfer(dma_addr_t framebuffer, unsigned int width,
                         unsigned int height)
{
   writel(framebuffer, (void __iomem *)(OMAP2_DISPC_BASE + DISPC_GFX_BA0));
   writel(((height - 1) << 16) | (width - 1),
          (void __iomem *)(OMAP2_DISPC_BASE + DISPC_GFX_SIZE));
   writel(width * height, (void __iomem *)(OMAP2_RFBI_BASE + RFBI_PIXELCNT));

   /*
    * ITE triggers a memory-to-RFBI transfer. QEMU models the same path as
    * the OMAP2 hardware: DISPC GFX_BA0 is the source and Blizzard is CS0.
    */
   writel(rfbi_control | RFBI_CONTROL_ITE,
          (void __iomem *)(OMAP2_RFBI_BASE + RFBI_CONTROL));
}
