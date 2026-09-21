#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "device.h"

#define MOTOR_FORWARD		1
#define MOTOR_REVERSE		-1
#define MOTOR_STEPS		512
#define MOTOR_INTERVAL_US	3000
#define VIBRATION_ALARM_COUNT	3

struct app_context
{
	struct device_context devices;
	struct system_config config;
	struct system_data data;
	pthread_mutex_t data_mutex;
	pthread_cond_t motor_cond;
	int motor_request_pending;
	int motor_target_open;
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
	int desired_open;

	while (!stop_requested)
	{
		if (!device_read_dht11(&context->devices, &sample))
		{
			pthread_mutex_lock(&context->data_mutex);
			context->data.temperature_c = sample.temperature_c;
			context->data.humidity_percent = sample.humidity_percent;
			context->data.dht11_valid = 1;
			context->data.temperature_alarm =
				sample.temperature_c >=
				context->config.temperature_limit_c;
			context->data.humidity_alarm =
				sample.humidity_percent >=
				context->config.humidity_limit_percent;

			desired_open = context->data.temperature_alarm ||
				       context->data.humidity_alarm;
			if (desired_open != context->motor_target_open)
			{
				context->motor_target_open = desired_open;
				context->motor_request_pending = 1;
				pthread_cond_signal(&context->motor_cond);
			}
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
	float previous_x = 0.0f;
	float previous_y = 0.0f;
	float previous_z = 0.0f;
	float dx;
	float dy;
	float dz;
	float vibration_mg;
	int previous_valid = 0;
	int abnormal_count = 0;

	while (!stop_requested)
	{
		if (!device_read_adxl345(&context->devices, &sample))
		{
			if (previous_valid)
			{
				dx = sample.acc_x_g - previous_x;
				dy = sample.acc_y_g - previous_y;
				dz = sample.acc_z_g - previous_z;
				vibration_mg = sqrtf(dx * dx + dy * dy + dz * dz)
					       * 1000.0f;
			}
			else
			{
				vibration_mg = 0.0f;
				previous_valid = 1;
			}

			previous_x = sample.acc_x_g;
			previous_y = sample.acc_y_g;
			previous_z = sample.acc_z_g;

			if (vibration_mg >= context->config.vibration_limit_mg)
			{
				if (abnormal_count < VIBRATION_ALARM_COUNT)
					abnormal_count++;
			}
			else
			{
				abnormal_count = 0;
			}

			pthread_mutex_lock(&context->data_mutex);
			context->data.acc_x_g = sample.acc_x_g;
			context->data.acc_y_g = sample.acc_y_g;
			context->data.acc_z_g = sample.acc_z_g;
			context->data.vibration_mg = vibration_mg;
			context->data.vibration_alarm =
				abnormal_count >= VIBRATION_ALARM_COUNT;
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

static void *motor_thread(void *argument)
{
	struct app_context *context = argument;
	int target_open;
	int ret;

	while (1)
	{
		pthread_mutex_lock(&context->data_mutex);
		while (!context->motor_request_pending && !stop_requested)
			pthread_cond_wait(&context->motor_cond , &context->data_mutex);

		if (stop_requested)
		{
			pthread_mutex_unlock(&context->data_mutex);
			break;
		}

		target_open = context->motor_target_open;
		context->motor_request_pending = 0;
		context->data.motor_running = 1;
		pthread_mutex_unlock(&context->data_mutex);

		ret = device_run_motor(&context->devices,
				       target_open ? MOTOR_FORWARD : MOTOR_REVERSE,
				       MOTOR_STEPS, MOTOR_INTERVAL_US);

		pthread_mutex_lock(&context->data_mutex);
		context->data.motor_running = 0;
		if (!ret)
		{
			context->data.motor_open = target_open;
		}
		else
		{
			fprintf(stderr, "Run motor failed: %s\n", strerror(errno));
			if (!context->motor_request_pending)
				context->motor_target_open = context->data.motor_open;
		}
		pthread_mutex_unlock(&context->data_mutex);
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

static const char *alarm_text(int alarm)
{
	return alarm ? "ALARM" : "normal";
}

static void print_data(const struct system_data *data)
{
	printf("\nSystem state:\n");
	if (data->dht11_valid)
	{
		printf("  temperature : %5.1f C    [%s]\n", data->temperature_c,
		       alarm_text(data->temperature_alarm));
		printf("  humidity    : %5.1f %%RH  [%s]\n", data->humidity_percent,
		       alarm_text(data->humidity_alarm));
	}
	else
	{
		printf("  DHT11       : waiting for first valid sample\n");
	}

	if (data->adxl345_valid)
	{
		printf("  acceleration: x=%7.3f g, y=%7.3f g, z=%7.3f g\n",
		       data->acc_x_g, data->acc_y_g, data->acc_z_g);
		printf("  vibration   : %7.1f mg   [%s]\n", data->vibration_mg,
		       alarm_text(data->vibration_alarm));
	}
	else
	{
		printf("  ADXL345     : waiting for first valid sample\n");
	}

	printf("  motor       : %s%s\n", data->motor_open ? "open" : "closed",
	       data->motor_running ? " (running)" : "");
	fflush(stdout);
}

int main(void)
{
	struct app_context context;
	struct system_data snapshot;
	pthread_t dht11_tid;
	pthread_t adxl345_tid;
	pthread_t motor_tid;
	int dht11_created = 0;
	int adxl345_created = 0;
	int motor_created = 0;
	int mutex_initialized = 0;
	int cond_initialized = 0;
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
		device_default_config(&context.config);
		if (device_save_config(&context.devices, &context.config))
		{
			perror("Save default configuration");
			goto err_exit;
		}
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

	ret = pthread_cond_init(&context.motor_cond, NULL);
	if (ret)
	{
		errno = ret;
		perror("pthread_cond_init");
		goto err_threads;
	}
	cond_initialized = 1;

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
	adxl345_created = 1;

	ret = pthread_create(&motor_tid, NULL, motor_thread, &context);
	if (ret)
	{
		errno = ret;
		perror("pthread_create motor");
		goto err_threads;
	}
	motor_created = 1;

	while (!stop_requested)
	{
		pthread_mutex_lock(&context.data_mutex);
		snapshot = context.data;
		pthread_mutex_unlock(&context.data_mutex);
		print_data(&snapshot);
		sleep_ms(1000);
	}

	printf("\nStopping threads...\n");
	pthread_mutex_lock(&context.data_mutex);
	pthread_cond_broadcast(&context.motor_cond);
	pthread_mutex_unlock(&context.data_mutex);
	pthread_join(dht11_tid, NULL);
	pthread_join(adxl345_tid, NULL);
	pthread_join(motor_tid, NULL);
	pthread_cond_destroy(&context.motor_cond);
	pthread_mutex_destroy(&context.data_mutex);
	device_exit(&context.devices);
	printf("Alarm control application exited.\n");
	return 0;

err_threads:
	stop_requested = 1;
	if (cond_initialized)
	{
		pthread_mutex_lock(&context.data_mutex);
		pthread_cond_broadcast(&context.motor_cond);
		pthread_mutex_unlock(&context.data_mutex);
	}
	if (dht11_created)
		pthread_join(dht11_tid, NULL);
	if (adxl345_created)
		pthread_join(adxl345_tid, NULL);
	if (motor_created)
		pthread_join(motor_tid, NULL);
	if (cond_initialized)
		pthread_cond_destroy(&context.motor_cond);
	if (mutex_initialized)
		pthread_mutex_destroy(&context.data_mutex);
err_exit:
	device_exit(&context.devices);
	return 1;
}
