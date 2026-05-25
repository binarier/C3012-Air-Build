/*
 * gpio_counter.c
 *
 * (c) 2017 Paweł Knioła <pawel.kn@gmail.com>
 *
 * Generic GPIO impulse counter. Counts impulses using GPIO interrupts.
 * See file: Documentation/input/misc/gpio-counter.txt for more information.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#define DRV_NAME "gpio-counter"

#define GPIO_COUNTER_ITEM_MAX 16

#define GPIO_COUNTER_DEFAULT_DIVIDER 1

struct gpio_counter_context_struct {
	u32 debounce_us;
	u32 divider;
	struct miscdevice miscdev;
	int total;
};

struct gpio_counter_struct {
	int gpio;
	bool inverted;
	struct delayed_work work;
	int irq;
	u64 count;
	s64 last_ns;
	bool last_state;
	bool last_stable_state;
	char name[16];
};

#define GPIO_COUNTER_FOR_EACH() \
	for (i = 0, counter = gc_counter; i < gc_context.total; i++, counter++)

static struct gpio_counter_context_struct gc_context;
static struct gpio_counter_struct gc_counter[GPIO_COUNTER_ITEM_MAX];

static ssize_t gpio_counter_read(struct file *file, char __user * userbuf, size_t count, loff_t * ppos)
{
	char buf[24];
	int len, total_len = 0;
	struct gpio_counter_struct *counter;
	int i;

	if (*ppos != 0)
		return 0;

	GPIO_COUNTER_FOR_EACH() {
		if (i + 1 < gc_context.total)
			len = snprintf(buf, sizeof(buf), "%llu ", counter->count / gc_context.divider);
		else
			len = snprintf(buf, sizeof(buf), "%llu\n", counter->count / gc_context.divider);

		total_len += len;

		if ((total_len < 0) || (total_len > count))
			return -EINVAL;

		if (copy_to_user(&userbuf[total_len - len], buf, len))
			return -EINVAL;
	}

	*ppos = total_len;
	return total_len;
}

static ssize_t gpio_counter_write(struct file *file, const char __user * userbuf, size_t count, loff_t * ppos)
{
	u64 value;
	struct gpio_counter_struct *counter;
	int i;

	if (kstrtoull_from_user(userbuf, count, 0, &value))
		return -EINVAL;

	GPIO_COUNTER_FOR_EACH() {
		counter->count = value;
	}

	return count;
}

static struct file_operations gpio_counter_fops = {
	.owner = THIS_MODULE,
	.read = gpio_counter_read,
	.write = gpio_counter_write,
};

static bool gpio_counter_get_state(const struct gpio_counter_struct *counter)
{
	bool state = gpio_get_value(counter->gpio);
	if (counter->inverted)
		state = !state;

	return state;
}

static s64 gpio_counter_get_time_nsec(void)
{
	struct timespec64 ts;
	ktime_get_ts64(&ts);
	return timespec64_to_ns(&ts);
}

#if 0
static void gpio_counter_process_state_change(struct gpio_counter_struct *counter)
{
	bool state;
	s64 current_ns;
	s64 delta_ns = 0;
	s64 debounce_ns;

	state = gpio_counter_get_state(counter);
	current_ns = gpio_counter_get_time_nsec();
	delta_ns = current_ns - counter->last_ns;
	debounce_ns = (s64)gc_context.debounce_us * NSEC_PER_USEC;

	if (delta_ns > debounce_ns) {
		if (counter->last_state && !counter->last_stable_state)
			counter->count++;

		counter->last_stable_state = counter->last_state;
	}

	counter->last_state = state;
	counter->last_ns = current_ns;
}

static void gpio_counter_delayed_work(struct work_struct *work)
{
	struct gpio_counter_struct *counter = container_of(work, struct gpio_counter_struct, work.work);
	gpio_counter_process_state_change(counter);
}
#endif

static irqreturn_t gpio_counter_irq(int irq, void *dev_id)
{
	struct gpio_counter_struct *counter = dev_id;

#if 0
	gpio_counter_process_state_change(counter);

	if (delayed_work_pending(&counter->work))
		cancel_delayed_work(&counter->work);

	schedule_delayed_work(&counter->work,
		usecs_to_jiffies(gc_context.debounce_us));
#else
	counter->count++;
#endif

	return IRQ_HANDLED;
}

static const struct of_device_id gpio_counter_of_match[] = {
	{ .compatible = "gpio-counter", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_counter_of_match);

static void *gpio_counter_parse_dt(struct device *dev)
{
	const struct of_device_id *of_id =
		of_match_device(gpio_counter_of_match, dev);
	struct device_node *np = dev->of_node;
	struct gpio_counter_struct *counter;
	enum of_gpio_flags flags;
	int i, total, err;

	if (!of_id || !np)
		return NULL;

	total = of_gpio_count(np);

	dev_info(dev, "found %d gpio(s)\n", total);

	if (total > GPIO_COUNTER_ITEM_MAX) {
		dev_err(dev, "too many gpios, total=%d\n", total);
		return NULL;
	}

	gc_context.total = total;

	err = of_property_read_u32(np, "divider", &gc_context.divider);
	if (err)
		gc_context.divider = GPIO_COUNTER_DEFAULT_DIVIDER;

	dev_info(dev, "divider is %u\n", gc_context.divider);

	err = of_property_read_u32(np, "debounce-delay-us", &gc_context.debounce_us);
	if (err)
		gc_context.debounce_us = 0;

	GPIO_COUNTER_FOR_EACH() {
		counter->gpio = of_get_gpio_flags(np, i, &flags);
		counter->inverted = flags & OF_GPIO_ACTIVE_LOW;
	}

	return &gc_context;
}

static int gpio_counter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const void *pdata = dev_get_platdata(dev);
	struct gpio_counter_struct *counter;
	int i, err;

	if (!pdata) {
		gpio_counter_parse_dt(dev);
	}

	GPIO_COUNTER_FOR_EACH() {
		counter->count = 0;

#if 0
		err = gpio_request_one(counter->gpio, GPIOF_IN, dev_name(dev));
		if (err) {
			dev_err(dev, "unable to request GPIO %d\n", counter->gpio);
			goto exit;
		}
#endif

		counter->last_stable_state = gpio_counter_get_state(counter);
		counter->last_state = counter->last_stable_state;
		counter->irq = gpio_to_irq(counter->gpio);
		counter->last_ns = gpio_counter_get_time_nsec();

		snprintf(counter->name, sizeof(counter->name), DRV_NAME"-%d", i);

		err = request_irq(counter->irq, &gpio_counter_irq,
				IRQF_TRIGGER_FALLING,
				counter->name, counter);
		if (err) {
			dev_err(dev, "unable to request IRQ %d\n", counter->irq);
			goto exit_free_gpio;
		}

#if 0
		INIT_DELAYED_WORK(&counter->work, gpio_counter_delayed_work);
#endif
	}

	gc_context.miscdev.minor  = MISC_DYNAMIC_MINOR;
	gc_context.miscdev.name   = dev_name(dev);
	gc_context.miscdev.fops   = &gpio_counter_fops;
	gc_context.miscdev.parent = dev;

	err = misc_register(&gc_context.miscdev);
	if (err) {
		dev_err(dev, "failed to register misc device\n");
		goto exit_free_irq;
	}

	dev_set_drvdata(dev, &gc_context);
	dev_info(dev, "registered new misc device %s\n", gc_context.miscdev.name);
	return 0;

exit_free_irq:
	while (i--) {
		counter = &gc_counter[i];
		free_irq(counter->irq, counter);
		/* gpio_free(counter->gpio); */
	}
exit_free_gpio:
	while (i--) {
		counter = &gc_counter[i];
		/* gpio_free(counter->gpio); */
	}
/* exit: */

	return err;
}

static int gpio_counter_remove(struct platform_device *pdev)
{
	struct gpio_counter_struct *counter;
	int i;

	device_init_wakeup(&pdev->dev, false);

	misc_deregister(&gc_context.miscdev);

	GPIO_COUNTER_FOR_EACH() {
		if (delayed_work_pending(&counter->work))
			cancel_delayed_work(&counter->work);

		free_irq(counter->irq, counter);
		/* gpio_free(counter->gpio); */
	}

	memset(&gc_context, 0, sizeof(gc_context));
	memset(gc_counter, 0, sizeof(gc_counter));

	return 0;
}

#ifdef CONFIG_PM
static int gpio_counter_suspend(struct device *dev)
{
	struct gpio_counter_struct *counter;
	int i;

	if (device_may_wakeup(dev)) {
		GPIO_COUNTER_FOR_EACH() {
			disable_irq_wake(counter->irq);
		}
	}

	return 0;
}

static int gpio_counter_resume(struct device *dev)
{
	struct gpio_counter_struct *counter;
	int i;

	if (device_may_wakeup(dev)) {
		GPIO_COUNTER_FOR_EACH() {
			enable_irq_wake(counter->irq);
		}
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(gpio_counter_pm_ops,
		gpio_counter_suspend, gpio_counter_resume);
#define GPIO_COUNTER_PM_OPS (&gpio_counter_pm_ops)
#else
#define GPIO_COUNTER_PM_OPS NULL
#endif /* CONFIG_PM */

static struct platform_driver gpio_counter_driver = {
	.probe		= gpio_counter_probe,
	.remove		= gpio_counter_remove,
	.driver		= {
		.name	= DRV_NAME,
		.pm	= GPIO_COUNTER_PM_OPS,
		.of_match_table = of_match_ptr(gpio_counter_of_match),
	}
};
module_platform_driver(gpio_counter_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic GPIO counter driver");
MODULE_AUTHOR("Paweł Knioła <pawel.kn@gmail.com>");
