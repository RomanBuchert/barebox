// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <driver.h>
#include <errno.h>
#include <io.h>
#include <mci.h>
#include <linux/err.h>

#define OMAP2420_MMC_CMD       0x00
#define OMAP2420_MMC_ARGL      0x04
#define OMAP2420_MMC_ARGH      0x08
#define OMAP2420_MMC_CON       0x0c
#define OMAP2420_MMC_STAT      0x10
#define OMAP2420_MMC_IE        0x14
#define OMAP2420_MMC_CTO       0x18
#define OMAP2420_MMC_DTO       0x1c
#define OMAP2420_MMC_DATA      0x20
#define OMAP2420_MMC_BLEN      0x24
#define OMAP2420_MMC_NBLK      0x28
#define OMAP2420_MMC_BUF       0x2c
#define OMAP2420_MMC_SYSC      0x64
#define OMAP2420_MMC_SYSS      0x68

#define OMAP2420_MMC_CON_DW4   BIT(15)
#define OMAP2420_MMC_CON_EN    BIT(11)

#define OMAP2420_MMC_CMD_DIR_READ   BIT(15)
#define OMAP2420_MMC_CMD_TYPE_BC    (0 << 12)
#define OMAP2420_MMC_CMD_TYPE_BCR   (1 << 12)
#define OMAP2420_MMC_CMD_TYPE_AC    (2 << 12)
#define OMAP2420_MMC_CMD_TYPE_ADTC  (3 << 12)
#define OMAP2420_MMC_CMD_BUSY       BIT(11)
#define OMAP2420_MMC_CMD_RSP_NONE   (0 << 8)
#define OMAP2420_MMC_CMD_RSP_R1     (1 << 8)
#define OMAP2420_MMC_CMD_RSP_R2     (2 << 8)
#define OMAP2420_MMC_CMD_RSP_R3     (3 << 8)
#define OMAP2420_MMC_CMD_RSP_R6     (6 << 8)
#define OMAP2420_MMC_CMD_INIT       BIT(7)

#define OMAP2420_MMC_STAT_CC        BIT(0)
#define OMAP2420_MMC_STAT_TC        BIT(3)
#define OMAP2420_MMC_STAT_CTO       BIT(7)
#define OMAP2420_MMC_STAT_BRR       BIT(10)
#define OMAP2420_MMC_STAT_BWR       BIT(11)
#define OMAP2420_MMC_STAT_ERR       BIT(14)

#define OMAP2420_MMC_SYSC_SRTS      BIT(2)
#define OMAP2420_MMC_SYSS_RSTD      BIT(0)

#define OMAP2420_MMC_WAIT_LOOPS     1000000
#define OMAP2420_MMC_INPUT_CLOCK    96000000

struct omap2420_mmc {
   struct mci_host mci;
   struct device *dev;
   void __iomem *base;
   unsigned int clock;
   enum mci_bus_width bus_width;
};

static inline struct omap2420_mmc *to_omap2420_mmc(struct mci_host *mci)
{
   return container_of(mci, struct omap2420_mmc, mci);
}

static inline u16 omap2420_mmc_readw(struct omap2420_mmc *host, unsigned int reg)
{
   return readw(host->base + reg);
}

static inline void omap2420_mmc_writew(struct omap2420_mmc *host, u16 value,
                                       unsigned int reg)
{
   writew(value, host->base + reg);
}

static int omap2420_mmc_wait_status(struct omap2420_mmc *host, u16 mask, u16 *status)
{
   unsigned int timeout = OMAP2420_MMC_WAIT_LOOPS;
   u16 stat;

   do {
      stat = omap2420_mmc_readw(host, OMAP2420_MMC_STAT);
      if (stat & (mask | OMAP2420_MMC_STAT_CTO | OMAP2420_MMC_STAT_ERR)) {
         *status = stat;
         return 0;
      }
   } while (--timeout);

   *status = omap2420_mmc_readw(host, OMAP2420_MMC_STAT);
   return -ETIMEDOUT;
}

static void omap2420_mmc_clear_status(struct omap2420_mmc *host)
{
   omap2420_mmc_writew(host, 0xffff, OMAP2420_MMC_STAT);
}

static unsigned int omap2420_mmc_response_type(const struct mci_cmd *cmd)
{
   if (!(cmd->resp_type & MMC_RSP_PRESENT))
      return OMAP2420_MMC_CMD_RSP_NONE;

   if (cmd->resp_type & MMC_RSP_136)
      return OMAP2420_MMC_CMD_RSP_R2;

   if (!(cmd->resp_type & MMC_RSP_CRC))
      return OMAP2420_MMC_CMD_RSP_R3;

   if (cmd->cmdidx == MMC_CMD_SET_RELATIVE_ADDR)
      return OMAP2420_MMC_CMD_RSP_R6;

   return OMAP2420_MMC_CMD_RSP_R1;
}

static void omap2420_mmc_read_response(struct omap2420_mmc *host, struct mci_cmd *cmd)
{
   u16 rsp[8];
   unsigned int i;

   if (!(cmd->resp_type & MMC_RSP_PRESENT))
      return;

   for (i = 0; i < ARRAY_SIZE(rsp); i++)
      rsp[i] = omap2420_mmc_readw(host, 0x40 + i * 4);

   if (cmd->resp_type & MMC_RSP_136) {
      cmd->response[3] = (u32)rsp[0] | ((u32)rsp[1] << 16);
      cmd->response[2] = (u32)rsp[2] | ((u32)rsp[3] << 16);
      cmd->response[1] = (u32)rsp[4] | ((u32)rsp[5] << 16);
      cmd->response[0] = (u32)rsp[6] | ((u32)rsp[7] << 16);
   } else {
      cmd->response[0] = (u32)rsp[6] | ((u32)rsp[7] << 16);
   }
}

static int omap2420_mmc_read_data(struct omap2420_mmc *host, struct mci_data *data)
{
   unsigned int remaining = data->blocks * data->blocksize;
   uint8_t *dest = data->dest;

   while (remaining) {
      u16 status;
      int ret;

      ret = omap2420_mmc_wait_status(host,
                                     OMAP2420_MMC_STAT_BRR | OMAP2420_MMC_STAT_TC,
                                     &status);
      if (ret)
         return ret;
      if (status & (OMAP2420_MMC_STAT_CTO | OMAP2420_MMC_STAT_ERR))
         return -EIO;

      if (status & OMAP2420_MMC_STAT_BRR) {
         unsigned int count = min(remaining, 64U);
         unsigned int i;

         for (i = 0; i < count; i += 2) {
            u16 value = omap2420_mmc_readw(host, OMAP2420_MMC_DATA);

            *dest++ = value & 0xff;
            if (i + 1 < count)
               *dest++ = value >> 8;
         }

         remaining -= count;
         omap2420_mmc_writew(host, OMAP2420_MMC_STAT_BRR, OMAP2420_MMC_STAT);
      }

      if ((status & OMAP2420_MMC_STAT_TC) && remaining)
         return -EIO;
   }

   return 0;
}

static int omap2420_mmc_write_data(struct omap2420_mmc *host, struct mci_data *data)
{
   unsigned int remaining = data->blocks * data->blocksize;
   const uint8_t *src = data->src;

   while (remaining) {
      u16 status;
      int ret;

      ret = omap2420_mmc_wait_status(host,
                                     OMAP2420_MMC_STAT_BWR | OMAP2420_MMC_STAT_TC,
                                     &status);
      if (ret)
         return ret;
      if (status & (OMAP2420_MMC_STAT_CTO | OMAP2420_MMC_STAT_ERR))
         return -EIO;

      if (status & OMAP2420_MMC_STAT_BWR) {
         unsigned int count = min(remaining, 64U);
         unsigned int i;

         for (i = 0; i < count; i += 2) {
            u16 value = *src++;

            if (i + 1 < count)
               value |= (u16)*src++ << 8;
            omap2420_mmc_writew(host, value, OMAP2420_MMC_DATA);
         }

         remaining -= count;
         omap2420_mmc_writew(host, OMAP2420_MMC_STAT_BWR, OMAP2420_MMC_STAT);
      }

      if ((status & OMAP2420_MMC_STAT_TC) && remaining)
         return -EIO;
   }

   return 0;
}

static int omap2420_mmc_wait_transfer_complete(struct omap2420_mmc *host)
{
   u16 status;
   int ret;

   ret = omap2420_mmc_wait_status(host, OMAP2420_MMC_STAT_TC, &status);
   if (ret)
      return ret;
   if (status & (OMAP2420_MMC_STAT_CTO | OMAP2420_MMC_STAT_ERR))
      return -EIO;

   omap2420_mmc_writew(host, OMAP2420_MMC_STAT_TC, OMAP2420_MMC_STAT);
   return 0;
}

static int omap2420_mmc_send_cmd(struct mci_host *mci, struct mci_cmd *cmd)
{
   struct omap2420_mmc *host = to_omap2420_mmc(mci);
   struct mci_data *data = cmd->data;
   unsigned int command;
   u16 status;
   int ret;

   omap2420_mmc_clear_status(host);

   omap2420_mmc_writew(host, cmd->cmdarg & 0xffff, OMAP2420_MMC_ARGL);
   omap2420_mmc_writew(host, cmd->cmdarg >> 16, OMAP2420_MMC_ARGH);

   command = cmd->cmdidx & 0x3f;
   command |= omap2420_mmc_response_type(cmd);

   if (data) {
      if (!data->blocks || !data->blocksize || data->blocksize > 2048)
         return -EINVAL;

      omap2420_mmc_writew(host, data->blocksize - 1, OMAP2420_MMC_BLEN);
      omap2420_mmc_writew(host, data->blocks - 1, OMAP2420_MMC_NBLK);
      command |= OMAP2420_MMC_CMD_TYPE_ADTC;
      if (data->flags & MMC_DATA_READ)
         command |= OMAP2420_MMC_CMD_DIR_READ;
   } else if (!(cmd->resp_type & MMC_RSP_PRESENT)) {
      command |= OMAP2420_MMC_CMD_TYPE_BC;
   } else if (cmd->cmdidx == MMC_CMD_SEND_OP_COND || cmd->cmdidx == SD_CMD_APP_SEND_OP_COND) {
      command |= OMAP2420_MMC_CMD_TYPE_BCR;
   } else {
      command |= OMAP2420_MMC_CMD_TYPE_AC;
   }

   if (cmd->resp_type & MMC_RSP_BUSY)
      command |= OMAP2420_MMC_CMD_BUSY;

   if (cmd->cmdidx == 2 || cmd->cmdidx == 3 || cmd->cmdidx == 7 ||
       cmd->cmdidx == 8 || cmd->cmdidx == 9 || cmd->cmdidx == 41 ||
       cmd->cmdidx == 55 || cmd->cmdidx == 51) {
      dev_info(host->dev,
               "CMD%u: arg=0x%08x resp=0x%02x hw=0x%04x data=%u/%u\n",
               cmd->cmdidx, cmd->cmdarg, cmd->resp_type, command,
               data ? data->blocks : 0, data ? data->blocksize : 0);
   }

   omap2420_mmc_writew(host, command, OMAP2420_MMC_CMD);

   ret = omap2420_mmc_wait_status(host, OMAP2420_MMC_STAT_CC, &status);
   if (ret) {
      dev_err(host->dev,
              "CMD%u: timeout waiting for command complete, STAT=0x%04x CON=0x%04x CMD=0x%04x\n",
              cmd->cmdidx, status,
              omap2420_mmc_readw(host, OMAP2420_MMC_CON),
              omap2420_mmc_readw(host, OMAP2420_MMC_CMD));
      return ret;
   }
   if (status & OMAP2420_MMC_STAT_CTO) {
      dev_err(host->dev, "CMD%u: command timeout, STAT=0x%04x\n",
              cmd->cmdidx, status);
      return -ETIMEDOUT;
   }
   if (status & OMAP2420_MMC_STAT_ERR) {
      dev_err(host->dev, "CMD%u: card error, STAT=0x%04x\n",
              cmd->cmdidx, status);
      return -EIO;
   }

   omap2420_mmc_writew(host, OMAP2420_MMC_STAT_CC, OMAP2420_MMC_STAT);
   omap2420_mmc_read_response(host, cmd);

   if (cmd->cmdidx == 2 || cmd->cmdidx == 3 || cmd->cmdidx == 7 ||
       cmd->cmdidx == 8 || cmd->cmdidx == 9 || cmd->cmdidx == 41 ||
       cmd->cmdidx == 55 || cmd->cmdidx == 51) {
      dev_info(host->dev,
               "CMD%u: STAT=0x%04x RSP=%08x %08x %08x %08x\n",
               cmd->cmdidx, status,
               cmd->response[0], cmd->response[1],
               cmd->response[2], cmd->response[3]);
   }

   if (!data)
      return 0;

   if (data->flags & MMC_DATA_READ)
      ret = omap2420_mmc_read_data(host, data);
   else
      ret = omap2420_mmc_write_data(host, data);
   if (ret)
      return ret;

   return omap2420_mmc_wait_transfer_complete(host);
}

static void omap2420_mmc_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
   struct omap2420_mmc *host = to_omap2420_mmc(mci);
   unsigned int divider = 0;
   u16 con = OMAP2420_MMC_CON_EN;

   if (ios->clock) {
      divider = DIV_ROUND_UP(OMAP2420_MMC_INPUT_CLOCK, 2 * ios->clock);
      if (divider)
         divider--;
      divider = min(divider, 0x3ffU);
   }

   if (ios->bus_width == MMC_BUS_WIDTH_4)
      con |= OMAP2420_MMC_CON_DW4;

   con |= divider;
   omap2420_mmc_writew(host, con, OMAP2420_MMC_CON);
   host->clock = ios->clock;
   host->bus_width = ios->bus_width;
}

static int omap2420_mmc_init(struct mci_host *mci, struct device *dev)
{
   struct omap2420_mmc *host = to_omap2420_mmc(mci);
   unsigned int timeout = OMAP2420_MMC_WAIT_LOOPS;

   omap2420_mmc_writew(host, OMAP2420_MMC_SYSC_SRTS, OMAP2420_MMC_SYSC);
   while (!(omap2420_mmc_readw(host, OMAP2420_MMC_SYSS) & OMAP2420_MMC_SYSS_RSTD)) {
      if (!--timeout)
         return -ETIMEDOUT;
   }

   omap2420_mmc_writew(host, 0, OMAP2420_MMC_IE);
   omap2420_mmc_writew(host, 0xff, OMAP2420_MMC_CTO);
   omap2420_mmc_writew(host, 0xffff, OMAP2420_MMC_DTO);
   omap2420_mmc_writew(host, 0x1f1f, OMAP2420_MMC_BUF);
   omap2420_mmc_clear_status(host);

   dev_info(host->dev,
            "initialized: REV=0x%04x SYSS=0x%04x CON=0x%04x BUF=0x%04x\n",
            omap2420_mmc_readw(host, 0x3c),
            omap2420_mmc_readw(host, OMAP2420_MMC_SYSS),
            omap2420_mmc_readw(host, OMAP2420_MMC_CON),
            omap2420_mmc_readw(host, OMAP2420_MMC_BUF));

   return 0;
}

static const struct mci_ops omap2420_mmc_ops = {
   .init = omap2420_mmc_init,
   .set_ios = omap2420_mmc_set_ios,
   .send_cmd = omap2420_mmc_send_cmd,
};

static int omap2420_mmc_probe(struct device *dev)
{
   struct omap2420_mmc *host;
   struct resource *iores;

   host = xzalloc(sizeof(*host));
   host->dev = dev;
   host->mci.ops = omap2420_mmc_ops;
   host->mci.hw_dev = dev;
   host->mci.voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
   host->mci.host_caps = MMC_CAP_4_BIT_DATA;
   host->mci.f_min = 400000;
   host->mci.f_max = 24000000;

   iores = dev_request_mem_resource(dev, 0);
   if (IS_ERR(iores))
      return PTR_ERR(iores);
   host->base = IOMEM(iores->start);

   mci_of_parse(&host->mci);

   dev_info(dev, "OMAP2420 MMC controller at 0x%08llx\n",
            (unsigned long long)iores->start);

   return mci_register(&host->mci);
}

static const struct of_device_id omap2420_mmc_dt_ids[] = {
   { .compatible = "ti,omap2420-mmc" },
   { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, omap2420_mmc_dt_ids);

static struct driver omap2420_mmc_driver = {
   .name = "omap2420-mmc",
   .probe = omap2420_mmc_probe,
   .of_compatible = omap2420_mmc_dt_ids,
};
device_platform_driver(omap2420_mmc_driver);
