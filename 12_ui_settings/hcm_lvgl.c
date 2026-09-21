#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif
#include <time.h>

#include "device.h"
#include "ui.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"

#define MOTOR_FORWARD		1
#define MOTOR_REVERSE		-1
#define MOTOR_STEPS		512
#define MOTOR_INTERVAL_US	3000
#define VIBRATION_ALARM_COUNT	3
#define LOG_FILE_PATH		"/tmp/hcm_history.csv"
#define LOG_PERIOD_MS		5000
#define DISP_BUF_SIZE		(128 * 1024)
#define DISPLAY_WIDTH		1024
#define DISPLAY_HEIGHT		600
#define UI_UPDATE_PERIOD_MS	200

struct app_context
{
	struct device_context devices;
	struct system_config config;
	struct system_data data;
	pthread_mutex_t data_mutex;
	pthread_mutex_t config_mutex;
	pthread_cond_t motor_cond;
	int motor_request_pending;
	int motor_target_open;
};

static volatile sig_atomic_t stop_requested;

uint32_t custom_tick_get(void)
{
	static uint64_t start_ms;
	struct timespec now;
	uint64_t now_ms;

	clock_gettime(CLOCK_MONOTONIC, &now);
	now_ms = (uint64_t)now.tv_sec * 1000ULL +
		 (uint64_t)now.tv_nsec / 1000000ULL;
	if (!start_ms)
		start_ms = now_ms;
	return (uint32_t)(now_ms - start_ms);
}

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
	struct system_config config;
	int desired_open;

	while (!stop_requested)
	{
		pthread_mutex_lock(&context->config_mutex);
		config = context->config;
		pthread_mutex_unlock(&context->config_mutex);
		if (!device_read_dht11(&context->devices, &sample))
		{
			pthread_mutex_lock(&context->data_mutex);
			context->data.temperature_c = sample.temperature_c;
			context->data.humidity_percent = sample.humidity_percent;
			context->data.dht11_valid = 1;
			context->data.temperature_alarm =
				sample.temperature_c >=
				config.temperature_limit_c;
			context->data.humidity_alarm =
				sample.humidity_percent >=
				config.humidity_limit_percent;

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

		sleep_ms(config.dht11_period_ms);
	}
	return NULL;
}

static void *adxl345_thread(void *argument)
{
	struct app_context *context = argument;
	struct system_data sample = { 0 };
	struct system_config config;
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
		pthread_mutex_lock(&context->config_mutex);
		config = context->config;
		pthread_mutex_unlock(&context->config_mutex);
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

			if (vibration_mg >= config.vibration_limit_mg)
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

		sleep_ms(config.adxl345_period_ms);
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

static int write_log_header(FILE *file)
{
	long size;

	if (fseek(file, 0, SEEK_END) < 0)
		return -1;

	size = ftell(file);
	if (size < 0)
		return -1;

	if (!size)
	{
		if (fprintf(file,
			    "time,temperature_c,humidity_percent,"
			    "acc_x_g,acc_y_g,acc_z_g,vibration_mg,"
			    "temperature_alarm,humidity_alarm,"
			    "vibration_alarm,motor_open,motor_running\n") < 0)
			return -1;
		if (fflush(file) == EOF)
			return -1;
	}
	return 0;
}

static int write_log_data(FILE *file, const struct system_data *data)
{
	time_t now;
	struct tm local_time;
	char time_string[32];

	now = time(NULL);
	if (now == (time_t)-1 || !localtime_r(&now, &local_time))
		return -1;
	if (!strftime(time_string, sizeof(time_string),
		      "%Y-%m-%d %H:%M:%S", &local_time))
	{
		errno = EOVERFLOW;
		return -1;
	}

	if (fprintf(file, "%s,", time_string) < 0)
		return -1;

	if (data->dht11_valid)
	{
		if (fprintf(file, "%.1f,%.1f,", data->temperature_c,
			    data->humidity_percent) < 0)
			return -1;
	}
	else if (fprintf(file, "NA,NA,") < 0)
	{
		return -1;
	}

	if (data->adxl345_valid)
	{
		if (fprintf(file, "%.3f,%.3f,%.3f,%.1f,",
			    data->acc_x_g, data->acc_y_g, data->acc_z_g,
			    data->vibration_mg) < 0)
			return -1;
	}
	else if (fprintf(file, "NA,NA,NA,NA,") < 0)
	{
		return -1;
	}

	if (fprintf(file, "%d,%d,%d,%d,%d\n",
		    data->temperature_alarm, data->humidity_alarm,
		    data->vibration_alarm, data->motor_open,
		    data->motor_running) < 0)
		return -1;

	if (fflush(file) == EOF)
		return -1;
	return 0;
}

static void *log_thread(void *argument)
{
	struct app_context *context = argument;
	struct system_data snapshot;
	FILE *file;

	file = fopen(LOG_FILE_PATH, "a+");
	if (!file)
	{
		fprintf(stderr, "Open log file failed: %s\n", strerror(errno));
		return NULL;
	}

	if (write_log_header(file))
	{
		fprintf(stderr, "Write log header failed: %s\n", strerror(errno));
		fclose(file);
		return NULL;
	}

	printf("History log: %s, period=%d ms\n",
	       LOG_FILE_PATH, LOG_PERIOD_MS);

	while (!stop_requested)
	{
		pthread_mutex_lock(&context->data_mutex);
		snapshot = context->data;
		pthread_mutex_unlock(&context->data_mutex);

		if (write_log_data(file, &snapshot))
		{
			fprintf(stderr, "Write log data failed: %s\n",
				strerror(errno));
			break;
		}
		sleep_ms(LOG_PERIOD_MS);
	}

	if (fclose(file) == EOF)
		fprintf(stderr, "Close log file failed: %s\n", strerror(errno));
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

static void print_help(void)
{
	printf("\nCommands:\n");
	printf("  show\n");
	printf("  set temp <C>              (-40 to 80)\n");
	printf("  set humidity <%%RH>        (1 to 100)\n");
	printf("  set vibration <mg>        (1 to 16000)\n");
	printf("  set dht_period <ms>       (2000 to 60000)\n");
	printf("  set adxl_period <ms>      (10 to 10000)\n");
	printf("  save\n");
	printf("  help\n");
	printf("  quit\n");
	fflush(stdout);
}

static int set_config_value(struct system_config *config,
			    const char *name, long value)
{
	if (!strcmp(name, "temp") && value >= -40 && value <= 80)
		config->temperature_limit_c = value;
	else if (!strcmp(name, "humidity") && value >= 1 && value <= 100)
		config->humidity_limit_percent = value;
	else if (!strcmp(name, "vibration") && value >= 1 && value <= 16000)
		config->vibration_limit_mg = value;
	else if (!strcmp(name, "dht_period") &&
		 value >= 2000 && value <= 60000)
		config->dht11_period_ms = value;
	else if (!strcmp(name, "adxl_period") &&
		 value >= 10 && value <= 10000)
		config->adxl345_period_ms = value;
	else
		return -1;
	return 0;
}

static void *command_thread(void *argument)
{
	struct app_context *context = argument;
	struct system_config config;
	struct timeval timeout;
	fd_set read_fds;
	char line[128];
	char command[32];
	char name[32];
	char extra;
	char *end;
	long value;
	int ret;

	print_help();
	while (!stop_requested)
	{
		FD_ZERO(&read_fds);
		FD_SET(STDIN_FILENO, &read_fds);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		ret = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			fprintf(stderr, "select stdin failed: %s\n", strerror(errno));
			break;
		}
		if (!ret)
			continue;
		if (!fgets(line, sizeof(line), stdin))
			break;

		if (sscanf(line, " %31s %c", command, &extra) == 1 &&
		    !strcmp(command, "show"))
		{
			pthread_mutex_lock(&context->config_mutex);
			config = context->config;
			pthread_mutex_unlock(&context->config_mutex);
			print_config(&config);
		}
		else if (sscanf(line, " %31s %c", command, &extra) == 1 &&
			 !strcmp(command, "help"))
		{
			print_help();
		}
		else if (sscanf(line, " %31s %c", command, &extra) == 1 &&
			 !strcmp(command, "quit"))
		{
			stop_requested = 1;
			break;
		}
		else if (sscanf(line, " %31s %c", command, &extra) == 1 &&
			 !strcmp(command, "save"))
		{
			pthread_mutex_lock(&context->config_mutex);
			config = context->config;
			pthread_mutex_unlock(&context->config_mutex);
			if (device_save_config(&context->devices, &config))
				fprintf(stderr, "Save configuration failed: %s\n",
					strerror(errno));
			else
				printf("Configuration saved to EEPROM.\n");
		}
		else if (sscanf(line, " set %31s %31s %c",
				name, command, &extra) == 2)
		{
			errno = 0;
			value = strtol(command, &end, 10);
			if (errno || *end)
			{
				printf("Invalid number.\n");
				continue;
			}
			pthread_mutex_lock(&context->config_mutex);
			ret = set_config_value(&context->config, name, value);
			config = context->config;
			pthread_mutex_unlock(&context->config_mutex);
			if (ret)
				printf("Invalid parameter name or value.\n");
			else
			{
				printf("Configuration updated in memory.\n");
				print_config(&config);
			}
		}
		else
		{
			printf("Unknown command. Enter help for usage.\n");
		}
		fflush(stdout);
	}
	return NULL;
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

static void ui_get_config(struct system_config *config, void *user_data)
{
	struct app_context *context = user_data;

	pthread_mutex_lock(&context->config_mutex);
	*config = context->config;
	pthread_mutex_unlock(&context->config_mutex);
}

static void ui_apply_config(const struct system_config *config,
			    void *user_data)
{
	struct app_context *context = user_data;

	pthread_mutex_lock(&context->config_mutex);
	context->config = *config;
	pthread_mutex_unlock(&context->config_mutex);
}

int main(void)
{
	struct app_context context;
	struct ui_operations ui_operations;
	struct system_data snapshot;
	static lv_color_t draw_buffer[DISP_BUF_SIZE];
	static lv_disp_draw_buf_t display_buffer;
	static lv_disp_drv_t display_driver;
	static lv_indev_drv_t input_driver;
	pthread_t dht11_tid;
	pthread_t adxl345_tid;
	pthread_t motor_tid;
	pthread_t log_tid;
	pthread_t command_tid;
	int dht11_created = 0;
	int adxl345_created = 0;
	int motor_created = 0;
	int log_created = 0;
	int command_created = 0;
	int mutex_initialized = 0;
	int config_mutex_initialized = 0;
	int cond_initialized = 0;
	uint32_t now_ms;
	uint32_t next_ui_update;
	int ret;

	memset(&context, 0, sizeof(context));
	memset(&ui_operations, 0, sizeof(ui_operations));
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

	ret = pthread_mutex_init(&context.config_mutex, NULL);
	if (ret)
	{
		errno = ret;
		perror("pthread_mutex_init config");
		goto err_threads;
	}
	config_mutex_initialized = 1;

	ret = pthread_cond_init(&context.motor_cond, NULL);
	if (ret)
	{
		errno = ret;
		perror("pthread_cond_init");
		goto err_threads;
	}
	cond_initialized = 1;

	lv_init();
	fbdev_init();
	lv_disp_draw_buf_init(&display_buffer, draw_buffer, NULL,
			      DISP_BUF_SIZE);
	lv_disp_drv_init(&display_driver);
	display_driver.draw_buf = &display_buffer;
	display_driver.flush_cb = fbdev_flush;
	display_driver.hor_res = DISPLAY_WIDTH;
	display_driver.ver_res = DISPLAY_HEIGHT;
	lv_disp_drv_register(&display_driver);

	evdev_init();
	lv_indev_drv_init(&input_driver);
	input_driver.type = LV_INDEV_TYPE_POINTER;
	input_driver.read_cb = evdev_read;
	lv_indev_drv_register(&input_driver);
	ui_operations.get_config = ui_get_config;
	ui_operations.apply_config = ui_apply_config;
	ui_create(&ui_operations, &context);

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

	ret = pthread_create(&log_tid, NULL, log_thread, &context);
	if (ret)
	{
		errno = ret;
		perror("pthread_create log");
		goto err_threads;
	}
	log_created = 1;

	ret = pthread_create(&command_tid, NULL, command_thread, &context);
	if (ret)
	{
		errno = ret;
		perror("pthread_create command");
		goto err_threads;
	}
	command_created = 1;

	next_ui_update = custom_tick_get();
	while (!stop_requested)
	{
		now_ms = custom_tick_get();
		if ((int32_t)(now_ms - next_ui_update) >= 0)
		{
			pthread_mutex_lock(&context.data_mutex);
			snapshot = context.data;
			pthread_mutex_unlock(&context.data_mutex);

			ui_set_temperature(snapshot.temperature_c,
					   snapshot.temperature_alarm,
					   snapshot.dht11_valid);
			ui_set_humidity(snapshot.humidity_percent,
					snapshot.humidity_alarm,
					snapshot.dht11_valid);
			ui_set_acceleration(snapshot.acc_x_g,
					    snapshot.acc_y_g,
					    snapshot.acc_z_g,
					    snapshot.adxl345_valid);
			ui_set_vibration(snapshot.vibration_mg,
					 snapshot.vibration_alarm,
					 snapshot.adxl345_valid);
			ui_set_motor_state(snapshot.motor_open,
					   snapshot.motor_running);
			next_ui_update = now_ms + UI_UPDATE_PERIOD_MS;
		}

		lv_timer_handler();
		sleep_ms(5);
	}

	printf("\nStopping threads...\n");
	pthread_mutex_lock(&context.data_mutex);
	pthread_cond_broadcast(&context.motor_cond);
	pthread_mutex_unlock(&context.data_mutex);
	pthread_join(dht11_tid, NULL);
	pthread_join(adxl345_tid, NULL);
	pthread_join(motor_tid, NULL);
	pthread_join(log_tid, NULL);
	pthread_join(command_tid, NULL);
	pthread_cond_destroy(&context.motor_cond);
	pthread_mutex_destroy(&context.config_mutex);
	pthread_mutex_destroy(&context.data_mutex);
	device_exit(&context.devices);
	printf("System integration application exited.\n");
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
	if (log_created)
		pthread_join(log_tid, NULL);
	if (command_created)
		pthread_join(command_tid, NULL);
	if (cond_initialized)
		pthread_cond_destroy(&context.motor_cond);
	if (config_mutex_initialized)
		pthread_mutex_destroy(&context.config_mutex);
	if (mutex_initialized)
		pthread_mutex_destroy(&context.data_mutex);
err_exit:
	device_exit(&context.devices);
	return 1;
}
