// SPDX-License-Identifier: GPL-2.0
/*
 * DHT11 driver: start signal -> poll GPIO -> check data -> return by read().
 * The 20 ms start signal uses msleep(). Only the short data phase disables
 * preemption and local interrupts to protect the microsecond waveform.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kdev_t.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DHT11_INTERVAL_NS	2000000000LL
#define DHT11_START_MS		20
#define DHT11_WAIT_US		120
#define DHT11_BIT_ONE_US	50

struct dht11_sample
{
	unsigned char temperature;
	unsigned char humidity;
};

struct dht11_device
{
	struct gpio_desc *gpio;
	int major;
	struct class *class;
	struct mutex lock;
	s64 next_allowed_ns;
};

static struct dht11_device *dht11_dev;

/*
 * Wait until DATA changes from level, and return how long that level lasted.
 * ktime is used instead of a loop counter, so an interrupt cannot make a long
 * pulse look short. This function must only be used with a non-sleeping GPIO.
 */
static int dht11_wait_level(struct dht11_device *sensor, int level,
			    unsigned int timeout_us, s64 *width_ns)
{
	s64 start_ns;
	s64 now_ns;
	int value;

	start_ns = ktime_get_ns();
	for (;;)
	{
		value = gpiod_get_raw_value(sensor->gpio);
		if (value < 0)
			return value;
		if (value != level)
			break;

		now_ns = ktime_get_ns();
		if (now_ns - start_ns > (s64)timeout_us * 1000)
			return -ETIMEDOUT;
		cpu_relax();
	}

	now_ns = ktime_get_ns();
	if (width_ns)
		*width_ns = now_ns - start_ns;
	return 0;
}

/* Read the response pulse and 40 data bits. Preemption must be disabled. */
static int dht11_read_data(struct dht11_device *sensor, unsigned char data[5])
{
	s64 width_ns;
	int ret;
	int i;

	/* The bus is high first, then DHT11 starts its response low pulse. */
	ret = dht11_wait_level(sensor, 1, DHT11_WAIT_US, NULL);
	if (ret)
		return ret;

	/* DHT11 response: about 80 us low and about 80 us high. */
	ret = dht11_wait_level(sensor, 0, DHT11_WAIT_US, NULL);
	if (ret)
		return ret;
	ret = dht11_wait_level(sensor, 1, DHT11_WAIT_US, NULL);
	if (ret)
		return ret;

	for (i = 0; i < 40; i++)
	{
		/* Every bit starts with an approximately 50 us low pulse. */
		ret = dht11_wait_level(sensor, 0, DHT11_WAIT_US, NULL);
		if (ret)
			return ret;

		/* About 26-28 us means 0; about 70 us means 1. */
		ret = dht11_wait_level(sensor, 1, DHT11_WAIT_US, &width_ns);
		if (ret)
			return ret;

		data[i / 8] <<= 1;
		if (width_ns > (s64)DHT11_BIT_ONE_US * 1000)
			data[i / 8] |= 1;
	}

	return 0;
}

static int dht11_capture(struct dht11_device *sensor,
			 struct dht11_sample *sample)
{
	unsigned char data[5] = { 0 };
	unsigned long flags;
	s64 now_ns;
	int ret;

	now_ns = ktime_get_ns();
	if (now_ns < sensor->next_allowed_ns)
		return -EAGAIN;
	sensor->next_allowed_ns = now_ns + DHT11_INTERVAL_NS;

	/* Host start signal. Sleeping here does not waste CPU time. */
	ret = gpiod_direction_output_raw(sensor->gpio, 0);
	if (ret)
		return ret;
	msleep(DHT11_START_MS);

	/*
	 * DHT11 data pulses are only tens of microseconds wide. Disable local
	 * interrupts only while receiving the response and 40 data bits, so an
	 * interrupt handler cannot make a pulse look longer or hide a transition.
	 * The 20 ms start signal above still sleeps normally.
	 */
	preempt_disable();
	local_irq_save(flags);
	ret = gpiod_direction_input(sensor->gpio);
	if (!ret)
		ret = dht11_read_data(sensor, data);
	local_irq_restore(flags);
	preempt_enable();

	/* Leave DATA released after every attempt. */
	if (ret)
		return ret;

	if (((data[0] + data[1] + data[2] + data[3]) & 0xff) != data[4])
		return -EIO;

	/* DHT11 normally reports integer humidity and temperature. */
	if (data[0] > 100 || data[2] > 80)
		return -EIO;

	sample->humidity = data[0];
	sample->temperature = data[2];
	return 0;
}

static ssize_t dht11_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct dht11_device *sensor = dht11_dev;
	struct dht11_sample sample;
	int ret;

	if (!count)
		return 0;
	if (!sensor || iminor(file_inode(file)) != 0)
		return -ENODEV;
	if (count < sizeof(sample))
		return -EINVAL;
	if (file->f_flags & O_NONBLOCK)
		return -EOPNOTSUPP;

	ret = mutex_lock_interruptible(&sensor->lock);
	if (ret)
		return ret;
	ret = dht11_capture(sensor, &sample);
	mutex_unlock(&sensor->lock);
	if (ret)
		return ret;

	if (copy_to_user(buf, &sample, sizeof(sample)))
		return -EFAULT;
	return sizeof(sample);
}

static const struct file_operations dht11_fops =
{
	.owner = THIS_MODULE,
	.read = dht11_read,
	.llseek = no_llseek,
};

static int dht11_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dht11_device *sensor;
	struct device *device;
	int ret;

	if (dht11_dev)
		return -EBUSY;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	mutex_init(&sensor->lock);
	sensor->gpio = devm_gpiod_get(dev, "data", GPIOD_IN);
	if (IS_ERR(sensor->gpio))
		return PTR_ERR(sensor->gpio);
	if (gpiod_cansleep(sensor->gpio))
		return -EINVAL;

	sensor->major = register_chrdev(0, "dht11", &dht11_fops);
	if (sensor->major < 0)
		return sensor->major;

	sensor->class = class_create(THIS_MODULE, "dht11");
	if (IS_ERR(sensor->class))
	{
		ret = PTR_ERR(sensor->class);
		goto err_unregister_chrdev;
	}

	device = device_create(sensor->class, dev, MKDEV(sensor->major, 0),
			       NULL, "hcm_dht11");
	if (IS_ERR(device))
	{
		ret = PTR_ERR(device);
		goto err_destroy_class;
	}

	platform_set_drvdata(pdev, sensor);
	dht11_dev = sensor;
	dev_info(dev, "/dev/hcm_dht11 ready, major=%d\n", sensor->major);
	return 0;

err_destroy_class:
	class_destroy(sensor->class);
err_unregister_chrdev:
	unregister_chrdev(sensor->major, "dht11");
	return ret;
}

static int dht11_remove(struct platform_device *pdev)
{
	struct dht11_device *sensor = platform_get_drvdata(pdev);

	device_destroy(sensor->class, MKDEV(sensor->major, 0));
	class_destroy(sensor->class);
	unregister_chrdev(sensor->major, "dht11");
	dht11_dev = NULL;
	return 0;
}

static const struct of_device_id dht11_of_match[] =
{
	{ .compatible = "hcm,dht11" },
	{ }
};
MODULE_DEVICE_TABLE(of, dht11_of_match);

static struct platform_driver dht11_driver =
{
	.probe = dht11_probe,
	.remove = dht11_remove,
	.driver = {
		.name = "hcm-dht11",
		.of_match_table = dht11_of_match,
		.suppress_bind_attrs = true,
	},
};

static int __init dht11_init(void)
{
	return platform_driver_register(&dht11_driver);
}

static void __exit dht11_exit(void)
{
	platform_driver_unregister(&dht11_driver);
}

module_init(dht11_init);
module_exit(dht11_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DHT11 polling driver");
