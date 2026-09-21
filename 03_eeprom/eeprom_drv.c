// SPDX-License-Identifier: GPL-2.0
/*
 * AT24C02A I2C learning driver.
 * read/write access starts at file->f_pos; llseek changes the address.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define EEPROM_SIZE		256
#define EEPROM_PAGE_SIZE	8
#define EEPROM_WRITE_MS		10

struct hcm_eeprom_device
{
	struct i2c_client *client;
	int major;
	struct class *class;
	struct mutex lock;
};

static struct hcm_eeprom_device *eeprom_dev;

/*
 * Random read:
 * message 0 sends the EEPROM memory address;
 * message 1 reads data after a repeated START.
 */
static int eeprom_read_data(struct hcm_eeprom_device *eeprom,
			    u8 address, u8 *data, size_t length)
{
	struct i2c_msg messages[2];
	int ret;

	messages[0].addr = eeprom->client->addr;
	messages[0].flags = 0;
	messages[0].len = 1;
	messages[0].buf = &address;

	messages[1].addr = eeprom->client->addr;
	messages[1].flags = I2C_M_RD;
	messages[1].len = length;
	messages[1].buf = data;

	ret = i2c_transfer(eeprom->client->adapter, messages, 2);
	if (ret == 2)
		return 0;
	if (ret >= 0)
		return -EIO;
	return ret;
}

/* Write no more than one EEPROM page in one I2C transaction. */
static int eeprom_write_page(struct hcm_eeprom_device *eeprom,
			     u8 address, const u8 *data, size_t length)
{
	u8 tx_buf[EEPROM_PAGE_SIZE + 1];
	int ret;

	tx_buf[0] = address;
	memcpy(&tx_buf[1], data, length);

	ret = i2c_master_send(eeprom->client, tx_buf, length + 1);
	if (ret < 0)
		return ret;
	if (ret != length + 1)
		return -EIO;

	/* AT24C02A does not acknowledge while its internal write is active. */
	msleep(EEPROM_WRITE_MS);
	return 0;
}

static ssize_t eeprom_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct hcm_eeprom_device *eeprom = eeprom_dev;
	u8 data[EEPROM_SIZE];
	size_t length;
	int ret;

	if (!count)
		return 0;
	if (!eeprom || iminor(file_inode(file)) != 0)
		return -ENODEV;
	if (*ppos < 0)
		return -EINVAL;
	if (*ppos >= EEPROM_SIZE)
		return 0;

	length = min_t(size_t, count, EEPROM_SIZE - (size_t)*ppos);

	ret = mutex_lock_interruptible(&eeprom->lock);
	if (ret)
		return ret;

	ret = eeprom_read_data(eeprom, (u8)*ppos, data, length);
	if (ret)
		goto out_unlock;
	if (copy_to_user(buf, data, length))
	{
		ret = -EFAULT;
		goto out_unlock;
	}

	*ppos += length;
	mutex_unlock(&eeprom->lock);
	return length;

out_unlock:
	mutex_unlock(&eeprom->lock);
	return ret;
}

static ssize_t eeprom_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct hcm_eeprom_device *eeprom = eeprom_dev;
	u8 data[EEPROM_SIZE];
	size_t length;
	size_t written = 0;
	size_t page_left;
	size_t chunk;
	int ret;

	if (!count)
		return 0;
	if (!eeprom || iminor(file_inode(file)) != 0)
		return -ENODEV;
	if (*ppos < 0)
		return -EINVAL;
	if (*ppos >= EEPROM_SIZE)
		return -ENOSPC;

	length = min_t(size_t, count, EEPROM_SIZE - (size_t)*ppos);
	if (copy_from_user(data, buf, length))
		return -EFAULT;

	ret = mutex_lock_interruptible(&eeprom->lock);
	if (ret)
		return ret;

	while (written < length)
	{
		page_left = EEPROM_PAGE_SIZE - ((size_t)*ppos % EEPROM_PAGE_SIZE);
		chunk = min_t(size_t, length - written, page_left);

		ret = eeprom_write_page(eeprom, (u8)*ppos,
					&data[written], chunk);
		if (ret)
			break;

		*ppos += chunk;
		written += chunk;
	}

	mutex_unlock(&eeprom->lock);
	if (written)
		return written;
	return ret;
}

static loff_t eeprom_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t new_pos;

	switch (whence)
	{
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = file->f_pos + offset;
		break;
	case SEEK_END:
		new_pos = EEPROM_SIZE + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0 || new_pos > EEPROM_SIZE)
		return -EINVAL;

	file->f_pos = new_pos;
	return new_pos;
}

static const struct file_operations eeprom_fops =
{
	.owner = THIS_MODULE,
	.read = eeprom_read,
	.write = eeprom_write,
	.llseek = eeprom_llseek,
};

static int eeprom_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct hcm_eeprom_device *eeprom;
	struct device *device;
	u8 value;
	int ret;

	if (eeprom_dev)
		return -EBUSY;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	eeprom = devm_kzalloc(&client->dev, sizeof(*eeprom), GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	eeprom->client = client;
	mutex_init(&eeprom->lock);

	/* EEPROM has no ID register; a non-destructive read checks its ACK. */
	ret = eeprom_read_data(eeprom, 0, &value, 1);
	if (ret)
	{
		dev_err(&client->dev, "AT24C02A did not respond: %d\n", ret);
		return ret;
	}

	eeprom->major = register_chrdev(0, "hcm_eeprom", &eeprom_fops);
	if (eeprom->major < 0)
		return eeprom->major;

	eeprom->class = class_create(THIS_MODULE, "hcm_eeprom");
	if (IS_ERR(eeprom->class))
	{
		ret = PTR_ERR(eeprom->class);
		goto err_unregister_chrdev;
	}

	device = device_create(eeprom->class, &client->dev,
			       MKDEV(eeprom->major, 0), NULL, "hcm_eeprom");
	if (IS_ERR(device))
	{
		ret = PTR_ERR(device);
		goto err_destroy_class;
	}

	i2c_set_clientdata(client, eeprom);
	eeprom_dev = eeprom;
	dev_info(&client->dev,
		 "AT24C02A ready, address=0x%02x, /dev/hcm_eeprom\n",
		 client->addr);
	return 0;

err_destroy_class:
	class_destroy(eeprom->class);
err_unregister_chrdev:
	unregister_chrdev(eeprom->major, "hcm_eeprom");
	return ret;
}

static int eeprom_remove(struct i2c_client *client)
{
	struct hcm_eeprom_device *eeprom = i2c_get_clientdata(client);

	device_destroy(eeprom->class, MKDEV(eeprom->major, 0));
	class_destroy(eeprom->class);
	unregister_chrdev(eeprom->major, "hcm_eeprom");
	eeprom_dev = NULL;
	return 0;
}

static const struct of_device_id eeprom_of_match[] =
{
	{ .compatible = "hcm,at24c02" },
	{ }
};
MODULE_DEVICE_TABLE(of, eeprom_of_match);

static const struct i2c_device_id eeprom_id[] =
{
	{ "hcm_at24c02", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eeprom_id);

static struct i2c_driver eeprom_driver =
{
	.probe = eeprom_probe,
	.remove = eeprom_remove,
	.id_table = eeprom_id,
	.driver = {
		.name = "hcm_at24c02",
		.of_match_table = eeprom_of_match,
		.suppress_bind_attrs = true,
	},
};

module_i2c_driver(eeprom_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AT24C02A I2C learning driver");
