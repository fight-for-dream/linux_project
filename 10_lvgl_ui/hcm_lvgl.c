#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"

#include "ui.h"

#define DISP_BUF_SIZE	(128 * 1024)
#define DISPLAY_WIDTH	1024
#define DISPLAY_HEIGHT	600

uint32_t custom_tick_get(void)
{
	static uint64_t start_ms;
	struct timeval now;
	uint64_t now_ms;

	gettimeofday(&now, NULL);
	now_ms = ((uint64_t)now.tv_sec * 1000000ULL + now.tv_usec) / 1000ULL;
	if (!start_ms)
		start_ms = now_ms;
	return (uint32_t)(now_ms - start_ms);
}

int main(void)
{
	static lv_color_t draw_buffer[DISP_BUF_SIZE];
	static lv_disp_draw_buf_t display_buffer;
	static lv_disp_drv_t display_driver;
	static lv_indev_drv_t input_driver;

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

	ui_create();

	while (1)
	{
		lv_timer_handler();
		usleep(5000);
	}
	return 0;
}
