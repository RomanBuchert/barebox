// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nokia Retu multifunction device core.
 */

#include <common.h>
#include <driver.h>
#include <i2c/i2c.h>
#include <linux/mfd/core.h>
#include <mfd/retu.h>

struct retu_dev {
   struct i2c_client *client;
};

static const struct mfd_cell retu_cells[] = {
   { .name = "retu-wdt" },
};

int retu_read(struct retu_dev *rdev, unsigned int reg)
{
   int ret;

   ret = i2c_smbus_read_word_data(rdev->client, reg);
   return ret;
}
EXPORT_SYMBOL(retu_read);

int retu_write(struct retu_dev *rdev, unsigned int reg, u16 value)
{
   int ret;
   ret = i2c_smbus_write_word_data(rdev->client, reg, value);
   if (ret)
      dev_err(&rdev->client->dev, "write failed: %pe\n", ERR_PTR(ret));
   return ret;
}
EXPORT_SYMBOL(retu_write);

static int retu_probe(struct device *dev)
{
   struct retu_dev *rdev;
   int ret;

   rdev = xzalloc(sizeof(*rdev));
   rdev->client = to_i2c_client(dev);
   dev->priv = rdev;

   ret = retu_read(rdev, RETU_REG_WATCHDOG);
   if (ret < 0) {
      dev_err(dev, "failed to communicate with Retu: %pe\n", ERR_PTR(ret));
      return ret;
   }

   ret = mfd_add_devices(dev, retu_cells, ARRAY_SIZE(retu_cells));
   if (ret)
      dev_err(dev, "failed to register watchdog child: %pe\n", ERR_PTR(ret));

   return ret;
}

static const struct of_device_id retu_dt_ids[] = {
   { .compatible = "nokia,retu" },
   { }
};
MODULE_DEVICE_TABLE(of, retu_dt_ids);

static struct driver retu_driver = {
   .name = "retu",
   .probe = retu_probe,
   .of_compatible = DRV_OF_COMPAT(retu_dt_ids),
};
device_i2c_driver(retu_driver);
