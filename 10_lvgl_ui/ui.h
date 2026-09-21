#ifndef UI_H
#define UI_H

#include "lvgl/lvgl.h"

void ui_create(void);
void ui_set_temperature(float value, int alarm, int valid);
void ui_set_humidity(float value, int alarm, int valid);
void ui_set_acceleration(float x, float y, float z, int valid);
void ui_set_vibration(float value, int alarm, int valid);
void ui_set_motor_state(int open, int running);

#endif
