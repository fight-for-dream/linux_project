#ifndef SYSTEM_DATA_H
#define SYSTEM_DATA_H

#include <stdint.h>

#define SYSTEM_CONFIG_MAGIC	0x48434d31U
#define SYSTEM_CONFIG_VERSION	1U

struct system_config
{
	uint32_t magic;
	uint32_t version;
	int32_t temperature_limit_c;
	uint32_t humidity_limit_percent;
	uint32_t vibration_limit_mg;
	uint32_t dht11_period_ms;
	uint32_t adxl345_period_ms;
	uint32_t checksum;
};

struct system_data
{
	float temperature_c;
	float humidity_percent;
	float acc_x_g;
	float acc_y_g;
	float acc_z_g;
	float vibration_mg;
	int dht11_valid;
	int adxl345_valid;
	uint32_t dht11_error_count;
	uint32_t adxl345_error_count;
	int dht11_fault;
	int adxl345_fault;
	int temperature_alarm;
	int humidity_alarm;
	int vibration_alarm;
	int motor_running;
	int motor_open;
};

#endif
