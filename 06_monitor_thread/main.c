#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "device.h"

struct app_context
{
	struct device_context devices;
	struct system_config config;
	struct system_data data;
	pthread_mutex_t data_mutex;
};

static volatile sig_atomic_t stop_requested;

static void signal_handler(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static void sleep_ms(unsigned int milliseconds)
{
	struct timespec remaining;
	struct timespec request;

	request.tv_sec = milliseconds / 1000;
	request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (!stop_requested && nanosleep(&request, &remaining) < 0)
	{
		if (errno != EINTR)
			break;
		request = remaining;
	}
}

static void *dht11_thread(void *argument)
{
	struct app_context *context = argument;
	struct system_data sample = { 0 };

	while (!stop_requested)
	{
		if (!device_read_dht11(&context->devices, &sample))
		{
			pthread_mutex_lock(&context->data_mutex);
			context->data.temperature_c = sample.temperature_c;
			context->data.humidity_percent = sample.humidity_percent;
			context->data.dht11_valid = 1;
			pthread_mutex_unlock(&context->data_mutex);
		}
		else if (!stop_requested)
		{
			fprintf(stderr, "Read DHT11 failed: %s\n", strerror(errno));
		}

		sleep_ms(context->config.dht11_period_ms);
	}
	return NULL;
}

static void *adxl345_thread(void *argument)
{
	struct app_context *context = argument;
	struct system_data sample = { 0 };

	while (!stop_requested)
	{
		if (!device_read_adxl345(&context->devices, &sample))
		{
			pthread_mutex_lock(&context->data_mutex);
			context->data.acc_x_g = sample.acc_x_g;
			context->data.acc_y_g = sample.acc_y_g;
			context->data.acc_z_g = sample.acc_z_g;
			context->data.adxl345_valid = 1;
			pthread_mutex_unlock(&context->data_mutex);
		}
		else if (!stop_requested)
		{
			fprintf(stderr, "Read ADXL345 failed: %s\n", strerror(errno));
		}

		sleep_ms(context->config.adxl345_period_ms);
	}
	return NULL;
}

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
	printf("\nSensor data:\n");
	if (data->dht11_valid)
	{
		printf("  temperature : %.1f C\n", data->temperature_c);
		printf("  humidity    : %.1f %%RH\n", data->humidity_percent);
	}
	else
	{
		printf("  DHT11       : waiting for first valid sample\n");
	}

	if (data->adxl345_valid)
	{
		printf("  acceleration: x=%7.3f g, y=%7.3f g, z=%7.3f g\n",
		       data->acc_x_g, data->acc_y_g, data->acc_z_g);
	}
	else
	{
		printf("  ADXL345     : waiting for first valid sample\n");
	}
	fflush(stdout);
}

int main(void)
{
	struct app_context context;
	struct system_data snapshot;
	pthread_t dht11_tid;
	pthread_t adxl345_tid;
	int dht11_created = 0;
	int mutex_initialized = 0;
	int ret;

	memset(&context, 0, sizeof(context));
	stop_requested = 0;

	if (signal(SIGINT, signal_handler) == SIG_ERR ||
	    signal(SIGTERM, signal_handler) == SIG_ERR)
	{
		perror("signal");
		return 1;
	}

	if (device_init(&context.devices))
	{
		fprintf(stderr, "Open devices failed: %s\n", strerror(errno));
		return 1;
	}
	printf("All four device nodes opened successfully.\n");

	ret = device_load_config(&context.devices, &context.config);
	if (ret < 0)
	{
		perror("Load EEPROM configuration");
		goto err_exit;
	}
	if (ret > 0)
	{
		printf("EEPROM configuration is invalid; writing defaults.\n");
		device_default_config(&context.config);
		if (device_save_config(&context.devices, &context.config))
		{
			perror("Save default configuration");
			goto err_exit;
		}
	}
	else
	{
		printf("Configuration loaded from EEPROM.\n");
	}
	print_config(&context.config);

	ret = pthread_mutex_init(&context.data_mutex, NULL);
	if (ret)
	{
		errno = ret;
		perror("pthread_mutex_init");
		goto err_exit;
	}
	mutex_initialized = 1;

	ret = pthread_create(&dht11_tid, NULL, dht11_thread, &context);
	if (ret)
	{
		errno = ret;
		perror("pthread_create DHT11");
		goto err_threads;
	}
	dht11_created = 1;

	ret = pthread_create(&adxl345_tid, NULL, adxl345_thread, &context);
	if (ret)
	{
		errno = ret;
		perror("pthread_create ADXL345");
		goto err_threads;
	}

	while (!stop_requested)
	{
		pthread_mutex_lock(&context.data_mutex);
		snapshot = context.data;
		pthread_mutex_unlock(&context.data_mutex);

		print_data(&snapshot);
		sleep_ms(1000);
	}

	printf("\nStopping threads...\n");
	pthread_join(dht11_tid, NULL);
	pthread_join(adxl345_tid, NULL);
	pthread_mutex_destroy(&context.data_mutex);
	device_exit(&context.devices);
	printf("Monitor application exited.\n");
	return 0;

err_threads:
	stop_requested = 1;
	if (dht11_created)
		pthread_join(dht11_tid, NULL);
	if (mutex_initialized)
		pthread_mutex_destroy(&context.data_mutex);
err_exit:
	device_exit(&context.devices);
	return 1;
}
