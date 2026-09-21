#ifndef DEVICE_H
#define DEVICE_H

#include "system_data.h"

struct device_context
{
	int dht11_fd;
	int adxl345_fd;
	int eeprom_fd;
	int motor_fd;
};

int device_init(struct device_context *devices);
void device_exit(struct device_context *devices);

int device_read_dht11(struct device_context *devices,
			 struct system_data *data);
int device_read_adxl345(struct device_context *devices,
			   struct system_data *data);

/* Return 0 for valid data, 1 for invalid/uninitialized data, -1 for I/O error. */
int device_load_config(struct device_context *devices,
			  struct system_config *config);
int device_save_config(struct device_context *devices,
			  struct system_config *config);
void device_default_config(struct system_config *config);

int device_run_motor(struct device_context *devices, int direction,
		     unsigned int steps, unsigned int interval_us);

#endif
