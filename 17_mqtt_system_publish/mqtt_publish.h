#ifndef MQTT_PUBLISH_H
#define MQTT_PUBLISH_H

#include <signal.h>

struct mqtt_system_data
{
	float temperature_c;
	float humidity_percent;
	float acc_x_g;
	float acc_y_g;
	float acc_z_g;
	float vibration_mg;
	int dht11_valid;
	int adxl345_valid;
	int dht11_fault;
	int adxl345_fault;
	int temperature_alarm;
	int humidity_alarm;
	int vibration_alarm;
	int motor_open;
	int motor_running;
	int motor_auto_mode;
};

struct mqtt_publish_context
{
	const char *host;
	const char *port;
	volatile sig_atomic_t *stop_requested;
	void (*get_system_data)(struct mqtt_system_data *data,
				void *user_data);
	void *user_data;
};

void *mqtt_publish_thread(void *argument);

#endif
