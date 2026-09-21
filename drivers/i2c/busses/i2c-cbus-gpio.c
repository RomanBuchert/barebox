// SPDX-License-Identifier: GPL-2.0-only
/*
 * CBUS I2C adapter for Nokia Internet Tablets.
 *
 * The protocol implementation follows the Linux i2c-cbus-gpio driver.
 */

#include <common.h>
#include <driver.h>
#include <gpio.h>
#include <i2c/i2c.h>
#include <of_gpio.h>
#include <i2c/i2c-cbus-gpio.h>

#define CBUS_ADDR_BITS 3
#define CBUS_REG_BITS  5

struct cbus_host;
static struct cbus_host *cbus_default_host;

struct cbus_host {
   struct i2c_adapter adapter;
   struct device *dev;
   int clk_gpio;
   int dat_gpio;
   int sel_gpio;
};

static void cbus_send_bit(struct cbus_host *host, unsigned int bit)
{
   gpio_set_value(host->dat_gpio, bit ? 1 : 0);
   gpio_set_value(host->clk_gpio, 1);
   gpio_set_value(host->clk_gpio, 0);
}

static void cbus_send_data(struct cbus_host *host, unsigned int data, unsigned int len)
{
   int i;

   for (i = len; i > 0; i--)
      cbus_send_bit(host, data & BIT(i - 1));
}

static int cbus_receive_bit(struct cbus_host *host)
{
   int bit;

   gpio_set_value(host->clk_gpio, 1);
   bit = gpio_get_value(host->dat_gpio);
   gpio_set_value(host->clk_gpio, 0);

   return bit;
}

static int cbus_receive_word(struct cbus_host *host)
{
   int value = 0;
   int i;

   for (i = 16; i > 0; i--) {
      int bit = cbus_receive_bit(host);

      if (bit < 0)
         return bit;
      if (bit)
         value |= BIT(i - 1);
   }

   return value;
}

static int cbus_transfer(struct cbus_host *host, bool read, unsigned int dev,
                         unsigned int reg, unsigned int data)
{
   int ret = 0;

   gpio_set_value(host->sel_gpio, 0);

   ret = gpio_direction_output(host->dat_gpio, 1);
   if (ret)
      goto out_end;

   cbus_send_data(host, dev, CBUS_ADDR_BITS);
   cbus_send_bit(host, read);
   cbus_send_data(host, reg, CBUS_REG_BITS);

   if (!read) {
      cbus_send_data(host, data, 16);
   } else {
      ret = gpio_direction_input(host->dat_gpio);
      if (ret)
         goto out_end;

      gpio_set_value(host->clk_gpio, 1);
      ret = cbus_receive_word(host);
   }

out_end:
   gpio_set_value(host->sel_gpio, 1);
   gpio_set_value(host->clk_gpio, 1);
   gpio_set_value(host->clk_gpio, 0);

   return ret;
}

int cbus_gpio_read_reg(unsigned int dev, unsigned int reg)
{
   if (!cbus_default_host)
      return -ENODEV;

   return cbus_transfer(cbus_default_host, true, dev, reg, 0);
}
EXPORT_SYMBOL(cbus_gpio_read_reg);

int cbus_gpio_write_reg(unsigned int dev, unsigned int reg, unsigned int value)
{
   int ret;

   if (!cbus_default_host)
      return -ENODEV;

   ret = cbus_transfer(cbus_default_host, false, dev, reg, value);
   return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(cbus_gpio_write_reg);

static int cbus_master_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
   struct cbus_host *host = container_of(adapter, struct cbus_host, adapter);
   unsigned int reg;
   unsigned int value;
   int ret;

   if (num == 1 && !(msgs[0].flags & I2C_M_RD) && msgs[0].len == 3) {
      reg = msgs[0].buf[0];
      value = msgs[0].buf[1] | (msgs[0].buf[2] << 8);

      ret = cbus_transfer(host, false, msgs[0].addr, reg, value);
      return ret < 0 ? ret : num;
   }

   if (num == 2 && !(msgs[0].flags & I2C_M_RD) && msgs[0].len == 1 &&
       (msgs[1].flags & I2C_M_RD) && msgs[1].len == 2 && msgs[0].addr == msgs[1].addr) {
      reg = msgs[0].buf[0];
      ret = cbus_transfer(host, true, msgs[0].addr, reg, 0);
      if (ret < 0)
         return ret;

      msgs[1].buf[0] = ret & 0xff;
      msgs[1].buf[1] = ret >> 8;
      return num;
   }

   return -EOPNOTSUPP;
}

static int cbus_probe(struct device *dev)
{
   struct cbus_host *host;
   int ret;

   dev_info(dev, "probe\n");

   if (of_gpio_count(dev->of_node) != 3)
      return -EINVAL;

   host = xzalloc(sizeof(*host));
   host->dev = dev;

   host->clk_gpio = of_get_gpio(dev->of_node, 0);
   host->dat_gpio = of_get_gpio(dev->of_node, 1);
   host->sel_gpio = of_get_gpio(dev->of_node, 2);
   if (!gpio_is_valid(host->clk_gpio) || !gpio_is_valid(host->dat_gpio) ||
       !gpio_is_valid(host->sel_gpio))
      return -EINVAL;

   ret = gpio_request(host->clk_gpio, "CBUS clk");
   if (ret)
      return ret;

   ret = gpio_request(host->dat_gpio, "CBUS dat");
   if (ret)
      return ret;

   ret = gpio_request(host->sel_gpio, "CBUS sel");
   if (ret)
      return ret;

   gpio_direction_output(host->clk_gpio, 0);
   gpio_direction_input(host->dat_gpio);
   gpio_direction_output(host->sel_gpio, 1);

   host->adapter.master_xfer = cbus_master_xfer;
   host->adapter.dev.parent = dev;
   host->adapter.dev.of_node = dev->of_node;
   host->adapter.nr = -1;

   dev->priv = host;

   ret = i2c_add_numbered_adapter(&host->adapter);
   if (ret) {
      dev_err(dev, "failed to register adapter: %pe\n", ERR_PTR(ret));
   } else {
      cbus_default_host = host;
      dev_info(dev, "adapter registered\n");
   }

   return ret;
}

static const struct of_device_id cbus_dt_ids[] = {
   { .compatible = "i2c-cbus-gpio" },
   { }
};
MODULE_DEVICE_TABLE(of, cbus_dt_ids);

static struct driver cbus_driver = {
   .name = "i2c-cbus-gpio",
   .probe = cbus_probe,
   .of_compatible = DRV_OF_COMPAT(cbus_dt_ids),
};
device_platform_driver(cbus_driver);
