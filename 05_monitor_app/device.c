#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "device.h"

#define DHT11_RETRY_COUNT	5
#define ADXL345_SCALE_G		0.0039f
#define EEPROM_CONFIG_ADDRESS	0x00

struct dht11_sample
{
	unsigned char temperature;
	unsigned char humidity;
};

struct adxl345_sample
{
	int16_t x;
	int16_t y;
	int16_t z;
};

struct motor_command
{
	int32_t direction;
	uint32_t steps;
	uint32_t interval_us;
};

static uint32_t config_checksum(const struct system_config *config)
{
	const unsigned char *data = (const unsigned char *)config;
	uint32_t value = 2166136261U;
	size_t length = offsetof(struct system_config, checksum);
	size_t i;

	for (i = 0; i < length; i++)
	{
		value ^= data[i];
		value *= 16777619U;
	}
	return value;
}

static int config_valid(const struct system_config *config)
{
	if (config->magic != SYSTEM_CONFIG_MAGIC ||
	    config->version != SYSTEM_CONFIG_VERSION)
		return 0;
	if (config->checksum != config_checksum(config))
		return 0;
	if (config->temperature_limit_c < -40 ||
	    config->temperature_limit_c > 80)
		return 0;
	if (!config->humidity_limit_percent ||
	    config->humidity_limit_percent > 100)
		return 0;
	if (!config->vibration_limit_mg ||
	    config->vibration_limit_mg > 16000)
		return 0;
	if (config->dht11_period_ms < 2000 ||
	    config->dht11_period_ms > 60000)
		return 0;
	if (config->adxl345_period_ms < 10 ||
	    config->adxl345_period_ms > 10000)
		return 0;
	return 1;
}

static int read_exact(int fd, void *buf, size_t count)
{
	unsigned char *data = buf;
	size_t done = 0;
	ssize_t ret;

	while (done < count)
	{
		ret = read(fd, data + done, count - done);
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!ret)
		{
			errno = EIO;
			return -1;
		}
		done += ret;
	}
	return 0;
}

static int write_exact(int fd, const void *buf, size_t count)
{
	const unsigned char *data = buf;
	size_t done = 0;
	ssize_t ret;

	while (done < count)
	{
		ret = write(fd, data + done, count - done);
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (!ret)
		{
			errno = EIO;
			return -1;
		}
		done += ret;
	}
	return 0;
}

int device_init(struct device_context *devices)
{
	int saved_errno;

	devices->dht11_fd = -1;
	devices->adxl345_fd = -1;
	devices->eeprom_fd = -1;
	devices->motor_fd = -1;

	devices->dht11_fd = open("/dev/hcm_dht11", O_RDONLY);
	if (devices->dht11_fd < 0)
		goto err;

	devices->adxl345_fd = open("/dev/hcm_adxl345", O_RDONLY);
	if (devices->adxl345_fd < 0)
		goto err;

	devices->eeprom_fd = open("/dev/hcm_eeprom", O_RDWR);
	if (devices->eeprom_fd < 0)
		goto err;

	devices->motor_fd = open("/dev/hcm_motor", O_WRONLY);
	if (devices->motor_fd < 0)
		goto err;

	return 0;

err:
	saved_errno = errno;
	device_exit(devices);
	errno = saved_errno;
	return -1;
}

void device_exit(struct device_context *devices)
{
	if (devices->motor_fd >= 0)
		close(devices->motor_fd);
	if (devices->eeprom_fd >= 0)
		close(devices->eeprom_fd);
	if (devices->adxl345_fd >= 0)
		close(devices->adxl345_fd);
	if (devices->dht11_fd >= 0)
		close(devices->dht11_fd);

	devices->motor_fd = -1;
	devices->eeprom_fd = -1;
	devices->adxl345_fd = -1;
	devices->dht11_fd = -1;
}

int device_read_dht11(struct device_context *devices,
			 struct system_data *data)
{
	struct dht11_sample sample;
	ssize_t ret;
	int retry;

	for (retry = 0; retry < DHT11_RETRY_COUNT; retry++)
	{
		ret = read(devices->dht11_fd, &sample, sizeof(sample));
		if (ret == (ssize_t)sizeof(sample))
		{
			data->temperature_c = sample.temperature;
			data->humidity_percent = sample.humidity;
			return 0;
		}

		if (ret >= 0)
			errno = EIO;
		if (errno != EAGAIN && errno != EIO && errno != ETIMEDOUT)
			return -1;
		if (retry + 1 < DHT11_RETRY_COUNT)
			sleep(2);
	}

	return -1;
}

int device_read_adxl345(struct device_context *devices,
			   struct system_data *data)
{
	struct adxl345_sample sample;

	if (read_exact(devices->adxl345_fd, &sample, sizeof(sample)))
		return -1;

	data->acc_x_g = sample.x * ADXL345_SCALE_G;
	data->acc_y_g = sample.y * ADXL345_SCALE_G;
	data->acc_z_g = sample.z * ADXL345_SCALE_G;
	return 0;
}

void device_default_config(struct system_config *config)
{
	memset(config, 0, sizeof(*config));
	config->magic = SYSTEM_CONFIG_MAGIC;
	config->version = SYSTEM_CONFIG_VERSION;
	config->temperature_limit_c = 35;
	config->humidity_limit_percent = 80;
	config->vibration_limit_mg = 300;
	config->dht11_period_ms = 2000;
	config->adxl345_period_ms = 200;
	config->checksum = config_checksum(config);
}

int device_load_config(struct device_context *devices,
			  struct system_config *config)
{
	if (lseek(devices->eeprom_fd, EEPROM_CONFIG_ADDRESS, SEEK_SET) < 0)
		return -1;
	if (read_exact(devices->eeprom_fd, config, sizeof(*config)))
		return -1;

	if (!config_valid(config))
		return 1;
	return 0;
}

int device_save_config(struct device_context *devices,
			  struct system_config *config)
{
	config->magic = SYSTEM_CONFIG_MAGIC;
	config->version = SYSTEM_CONFIG_VERSION;
	config->checksum = config_checksum(config);

	if (lseek(devices->eeprom_fd, EEPROM_CONFIG_ADDRESS, SEEK_SET) < 0)
		return -1;
	return write_exact(devices->eeprom_fd, config, sizeof(*config));
}

int device_run_motor(struct device_context *devices, int direction,
		     unsigned int steps, unsigned int interval_us)
{
	struct motor_command command;
	ssize_t ret;

	command.direction = direction;
	command.steps = steps;
	command.interval_us = interval_us;

	ret = write(devices->motor_fd, &command, sizeof(command));
	if (ret == (ssize_t)sizeof(command))
		return 0;
	if (ret >= 0)
		errno = EIO;
	return -1;
}
