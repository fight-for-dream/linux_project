// SPDX-License-Identifier: GPL-2.0
/*
 * Four-GPIO stepper motor learning driver.
 * write() receives direction, step count and step interval.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MOTOR_GPIO_NUM		4
#define MOTOR_PHASE_NUM		8
#define MOTOR_MIN_INTERVAL_US	500
#define MOTOR_MAX_INTERVAL_US	100000
#define MOTOR_MAX_STEPS		8192

struct motor_command
{
	s32 direction;
	u32 steps;
	u32 interval_us;
};

struct motor_device
{
	struct gpio_desc *gpio[MOTOR_GPIO_NUM];
	int major;
	struct class *class;
	struct mutex lock;
	unsigned int phase;
};

/* Bit 0 drives GPIO4_IO19; bit 3 drives GPIO4_IO22. */
static const u8 motor_phase[MOTOR_PHASE_NUM] =
{
	0x2, 0x3, 0x1, 0x9, 0x8, 0xc, 0x4, 0x6
};

static struct motor_device *motor_dev;

static void motor_set_output(struct motor_device *motor, u8 value)
{
	int i;

	for (i = 0; i < MOTOR_GPIO_NUM; i++)
		gpiod_set_raw_value(motor->gpio[i], (value >> i) & 1);
}

static void motor_stop(struct motor_device *motor)
{
	motor_set_output(motor, 0);
}

static int motor_run(struct motor_device *motor,
		     const struct motor_command *command)
{
	u32 i;

	for (i = 0; i < command->steps; i++)
	{
		motor_set_output(motor, motor_phase[motor->phase]);

		if (command->direction > 0)
			motor->phase = (motor->phase + 1) & 7;
		else
			motor->phase = (motor->phase + 7) & 7;

		usleep_range(command->interval_us,
			     command->interval_us + 100);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	return 0;
}

static ssize_t motor_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct motor_device *motor = motor_dev;
	struct motor_command command;
	int ret;

	if (!motor || iminor(file_inode(file)) != 0)
		return -ENODEV;
	if (count != sizeof(command))
		return -EINVAL;
	if (copy_from_user(&command, buf, sizeof(command)))
		return -EFAULT;

	if (command.direction != 1 && command.direction != -1)
		return -EINVAL;
	if (!command.steps || command.steps > MOTOR_MAX_STEPS)
		return -EINVAL;
	if (command.interval_us < MOTOR_MIN_INTERVAL_US ||
	    command.interval_us > MOTOR_MAX_INTERVAL_US)
		return -EINVAL;

	ret = mutex_lock_interruptible(&motor->lock);
	if (ret)
		return ret;

	ret = motor_run(motor, &command);
	motor_stop(motor);
	mutex_unlock(&motor->lock);

	if (ret)
		return ret;
	return sizeof(command);
}

static const struct file_operations motor_fops =
{
	.owner = THIS_MODULE,
	.write = motor_write,
	.llseek = no_llseek,
};

static int motor_probe(struct platform_device *pdev)
{
	struct motor_device *motor;
	struct device *device;
	int ret;
	int i;

	if (motor_dev)
		return -EBUSY;

	motor = devm_kzalloc(&pdev->dev, sizeof(*motor), GFP_KERNEL);
	if (!motor)
		return -ENOMEM;

	mutex_init(&motor->lock);
	for (i = 0; i < MOTOR_GPIO_NUM; i++)
	{
		motor->gpio[i] = devm_gpiod_get_index(&pdev->dev, "motor", i,
						      GPIOD_OUT_LOW);
		if (IS_ERR(motor->gpio[i]))
			return PTR_ERR(motor->gpio[i]);
		if (gpiod_cansleep(motor->gpio[i]))
			return -EINVAL;
	}

	motor_stop(motor);
	motor->major = register_chrdev(0, "hcm_motor", &motor_fops);
	if (motor->major < 0)
		return motor->major;

	motor->class = class_create(THIS_MODULE, "hcm_motor");
	if (IS_ERR(motor->class))
	{
		ret = PTR_ERR(motor->class);
		goto err_unregister_chrdev;
	}

	device = device_create(motor->class, &pdev->dev,
			       MKDEV(motor->major, 0), NULL, "hcm_motor");
	if (IS_ERR(device))
	{
		ret = PTR_ERR(device);
		goto err_destroy_class;
	}

	platform_set_drvdata(pdev, motor);
	motor_dev = motor;
	dev_info(&pdev->dev, "/dev/hcm_motor ready\n");
	return 0;

err_destroy_class:
	class_destroy(motor->class);
err_unregister_chrdev:
	unregister_chrdev(motor->major, "hcm_motor");
	return ret;
}

static int motor_remove(struct platform_device *pdev)
{
	struct motor_device *motor = platform_get_drvdata(pdev);

	motor_stop(motor);
	device_destroy(motor->class, MKDEV(motor->major, 0));
	class_destroy(motor->class);
	unregister_chrdev(motor->major, "hcm_motor");
	motor_dev = NULL;
	return 0;
}

static const struct of_device_id motor_of_match[] =
{
	{ .compatible = "hcm,stepper-motor" },
	{ }
};
MODULE_DEVICE_TABLE(of, motor_of_match);

static struct platform_driver motor_driver =
{
	.probe = motor_probe,
	.remove = motor_remove,
	.driver = {
		.name = "hcm-stepper-motor",
		.of_match_table = motor_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(motor_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Four-GPIO stepper motor learning driver");
