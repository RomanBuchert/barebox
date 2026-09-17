// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog driver for the Nokia Retu multifunction device.
 */

#include <common.h>
#include <driver.h>
#include <mfd/retu.h>
#include <param.h>
#include <watchdog.h>

#define RETU_WDT_MAX_TIMEOUT 63

struct retu_wdt {
   struct watchdog wdd;
   struct retu_dev *rdev;
};

static int retu_wdt_set_timeout(struct watchdog *wdd, unsigned int timeout)
{
   struct retu_wdt *wdt = container_of(wdd, struct retu_wdt, wdd);

   if (!timeout)
      return -ENOSYS;

   return retu_write(wdt->rdev, RETU_REG_WATCHDOG, timeout);
}

static int retu_wdt_ping(struct watchdog *wdd)
{
   struct retu_wdt *wdt = container_of(wdd, struct retu_wdt, wdd);
   return retu_write(wdt->rdev, RETU_REG_WATCHDOG, wdd->timeout_cur);
}

static int retu_wdt_probe(struct device *dev)
{
   struct retu_wdt *wdt;
   struct watchdog *wdd;
   int ret;

   wdt = xzalloc(sizeof(*wdt));
   wdt->rdev = dev->parent->priv;
   if (!wdt->rdev)
      return -ENODEV;

   /*
    * Retu's watchdog cannot be disabled. Take ownership immediately and
    * extend the inherited bootloader timeout before registering the device.
    */
   ret = retu_write(wdt->rdev, RETU_REG_WATCHDOG, RETU_WDT_MAX_TIMEOUT);
   if (ret)
      return ret;

   wdd = &wdt->wdd;
   wdd->name = "retu";
   wdd->hwdev = dev;
   wdd->set_timeout = retu_wdt_set_timeout;
   wdd->ping = retu_wdt_ping;
   wdd->timeout_max = RETU_WDT_MAX_TIMEOUT;
   wdd->timeout_cur = RETU_WDT_MAX_TIMEOUT;
   wdd->running = WDOG_HW_RUNNING;
   ret = watchdog_register(wdd);
   if (ret)
      return ret;

   /* Retu cannot be stopped, so keep it serviced while Barebox is running. */
   ret = dev_set_param(&wdd->dev, "autoping", "1");
   if (ret)
      return ret;

   dev_info(dev, "Retu watchdog active, timeout %u s, autoping enabled\n",
            wdd->timeout_cur);

   return 0;
}

static struct driver retu_wdt_driver = {
   .name = "retu-wdt",
   .probe = retu_wdt_probe,
};
device_platform_driver(retu_wdt_driver);
