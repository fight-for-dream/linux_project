// SPDX-License-Identifier: GPL-2.0
/*
 * ADXL345 SPI learning driver.
 * probe: check device ID and initialize sensor.
 * read : return X/Y/Z raw data to user space.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define ADXL345_REG_DEVID	0x00
#define ADXL345_REG_BW_RATE	0x2c
#define ADXL345_REG_POWER_CTL	0x2d
#define ADXL345_REG_DATA_FORMAT	0x31
#define ADXL345_REG_DATAX0	0x32

#define ADXL345_DEVID		0xe5
#define ADXL345_READ		0x80
#define ADXL345_MULTI_BYTE	0x40

#define ADXL345_RATE_100HZ	0x0a
#define ADXL345_MEASURE		0x08
#define ADXL345_FULL_RES_2G	0x08

struct adxl345_sample
{
	s16 x;
	s16 y;
	s16 z;
};

struct adxl345_device
{
	struct spi_device *spi;
	int major;
	struct class *class;
	struct mutex lock;
};

static struct adxl345_device *adxl345_dev;

static int adxl345_read_reg(struct adxl345_device *sensor,
			    u8 reg, u8 *value)
{
	u8 command = reg | ADXL345_READ;

	return spi_write_then_read(sensor->spi, &command, 1, value, 1);
}

static int adxl345_write_reg(struct adxl345_device *sensor,
			     u8 reg, u8 value)
{
	u8 tx_buf[2];

	tx_buf[0] = reg;
	tx_buf[1] = value;
	return spi_write(sensor->spi, tx_buf, sizeof(tx_buf));
}

static int adxl345_read_xyz(struct adxl345_device *sensor,
			    struct adxl345_sample *sample)
{
	u8 command;
	u8 data[6];
	int ret;

	/* R/W=1 and MB=1: read six consecutive data registers. */
	command = ADXL345_REG_DATAX0 | ADXL345_READ | ADXL345_MULTI_BYTE;
	ret = spi_write_then_read(sensor->spi, &command, 1,
				  data, sizeof(data));
	if (ret)
		return ret;

	/* ADXL345 data is little-endian two's complement. */
	sample->x = (s16)(((u16)data[1] << 8) | data[0]);
	sample->y = (s16)(((u16)data[3] << 8) | data[2]);
	sample->z = (s16)(((u16)data[5] << 8) | data[4]);
	return 0;
}

static ssize_t adxl345_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct adxl345_device *sensor = adxl345_dev;
	struct adxl345_sample sample;
	int ret;

	if (!count)
		return 0;
	if (!sensor || iminor(file_inode(file)) != 0)
		return -ENODEV;
	if (count < sizeof(sample))
		return -EINVAL;

	ret = mutex_lock_interruptible(&sensor->lock);
	if (ret)
		return ret;
	ret = adxl345_read_xyz(sensor, &sample);
	mutex_unlock(&sensor->lock);
	if (ret)
		return ret;

	if (copy_to_user(buf, &sample, sizeof(sample)))
		return -EFAULT;
	return sizeof(sample);
}

static const struct file_operations adxl345_fops =
{
	.owner = THIS_MODULE,
	.read = adxl345_read,
	.llseek = no_llseek,
};

static int adxl345_probe(struct spi_device *spi)
{
	struct adxl345_device *sensor;
	struct device *device;
	u8 device_id;
	int ret;

	if (adxl345_dev)
		return -EBUSY;

	sensor = devm_kzalloc(&spi->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->spi = spi;
	mutex_init(&sensor->lock);

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	ret = adxl345_read_reg(sensor, ADXL345_REG_DEVID, &device_id);
	if (ret)
		return ret;
	if (device_id != ADXL345_DEVID)
	{
		dev_err(&spi->dev, "wrong device ID: 0x%02x\n", device_id);
		return -ENODEV;
	}

	/* Configure the sensor in standby, then enter measurement mode. */
	ret = adxl345_write_reg(sensor, ADXL345_REG_POWER_CTL, 0x00);
	if (ret)
		return ret;
	ret = adxl345_write_reg(sensor, ADXL345_REG_DATA_FORMAT,
				ADXL345_FULL_RES_2G);
	if (ret)
		return ret;
	ret = adxl345_write_reg(sensor, ADXL345_REG_BW_RATE,
				ADXL345_RATE_100HZ);
	if (ret)
		return ret;
	ret = adxl345_write_reg(sensor, ADXL345_REG_POWER_CTL,
				ADXL345_MEASURE);
	if (ret)
		return ret;

	sensor->major = register_chrdev(0, "adxl345", &adxl345_fops);
	if (sensor->major < 0)
		return sensor->major;

	sensor->class = class_create(THIS_MODULE, "adxl345");
	if (IS_ERR(sensor->class))
	{
		ret = PTR_ERR(sensor->class);
		goto err_standby;
	}

	device = device_create(sensor->class, &spi->dev,
			       MKDEV(sensor->major, 0), NULL, "hcm_adxl345");
	if (IS_ERR(device))
	{
		ret = PTR_ERR(device);
		goto err_destroy_class;
	}

	spi_set_drvdata(spi, sensor);
	adxl345_dev = sensor;
	dev_info(&spi->dev,
		 "ADXL345 ready, ID=0x%02x, /dev/hcm_adxl345\n",
		 device_id);
	return 0;

err_destroy_class:
	class_destroy(sensor->class);
err_standby:
	unregister_chrdev(sensor->major, "adxl345");
	adxl345_write_reg(sensor, ADXL345_REG_POWER_CTL, 0x00);
	return ret;
}

static int adxl345_remove(struct spi_device *spi)
{
	struct adxl345_device *sensor = spi_get_drvdata(spi);

	device_destroy(sensor->class, MKDEV(sensor->major, 0));
	class_destroy(sensor->class);
	unregister_chrdev(sensor->major, "adxl345");
	adxl345_write_reg(sensor, ADXL345_REG_POWER_CTL, 0x00);
	adxl345_dev = NULL;
	return 0;
}

static const struct of_device_id adxl345_of_match[] =
{
	{ .compatible = "hcm,adxl345" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl345_of_match);

static struct spi_driver adxl345_driver =
{
	.probe = adxl345_probe,
	.remove = adxl345_remove,
	.driver = {
		.name = "hcm-adxl345",
		.of_match_table = adxl345_of_match,
		.suppress_bind_attrs = true,
	},
};

module_spi_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ADXL345 SPI learning driver");
