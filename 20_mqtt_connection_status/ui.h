#ifndef UI_H
#define UI_H

#include "lvgl/lvgl.h"
#include "system_data.h"

struct ui_operations
{
	void (*get_config)(struct system_config *config, void *user_data);
	void (*apply_config)(const struct system_config *config, void *user_data);
	void (*request_save)(void *user_data);
	void (*get_motor_control)(int *auto_mode, int *open, int *running,
				  void *user_data);
	void (*set_motor_mode)(int auto_mode, void *user_data);
	void (*request_motor)(int open, void *user_data);
};

void ui_create(const struct ui_operations *operations, void *user_data);
void ui_set_temperature(float value, int alarm, int valid, int fault);
void ui_set_humidity(float value, int alarm, int valid, int fault);
void ui_set_acceleration(float x, float y, float z, int valid, int fault);
void ui_set_vibration(float value, int alarm, int valid, int fault);
void ui_set_motor_state(int open, int running, int auto_mode);
void ui_set_save_status(int status);
void ui_set_mqtt_state(int online);

#endif
