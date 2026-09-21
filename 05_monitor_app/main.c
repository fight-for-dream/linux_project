#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "device.h"

static void print_config(const struct system_config *config)
{
	printf("Configuration:\n");
	printf("  temperature limit : %d C\n", config->temperature_limit_c);
	printf("  humidity limit    : %u %%RH\n",
	       config->humidity_limit_percent);
	printf("  vibration limit   : %u mg\n", config->vibration_limit_mg);
	printf("  DHT11 period      : %u ms\n", config->dht11_period_ms);
	printf("  ADXL345 period    : %u ms\n", config->adxl345_period_ms);
}

static void print_data(const struct system_data *data)
{
	printf("Sensor data:\n");
	printf("  temperature : %.1f C\n", data->temperature_c);
	printf("  humidity    : %.1f %%RH\n", data->humidity_percent);
	printf("  acceleration: x=%7.3f g, y=%7.3f g, z=%7.3f g\n",
	       data->acc_x_g, data->acc_y_g, data->acc_z_g);
}

int main(void)
{
	struct device_context devices;
	struct system_config config;
	struct system_data data = { 0 };
	int ret;

	if (device_init(&devices))
	{
		fprintf(stderr, "Open devices failed: %s\n", strerror(errno));
		return 1;
	}
	printf("All four device nodes opened successfully.\n");

	ret = device_load_config(&devices, &config);
	if (ret < 0)
	{
		perror("Load EEPROM configuration");
		goto err_exit;
	}
	if (ret > 0)
	{
		printf("EEPROM configuration is invalid; writing defaults.\n");
		device_default_config(&config);
		if (device_save_config(&devices, &config))
		{
			perror("Save default configuration");
			goto err_exit;
		}
	}

	if (device_read_dht11(&devices, &data))
	{
		perror("Read DHT11");
		goto err_exit;
	}
	if (device_read_adxl345(&devices, &data))
	{
		perror("Read ADXL345");
		goto err_exit;
	}

	printf("\n");
	print_config(&config);
	printf("\n");
	print_data(&data);

	device_exit(&devices);
	return 0;

err_exit:
	device_exit(&devices);
	return 1;
}
