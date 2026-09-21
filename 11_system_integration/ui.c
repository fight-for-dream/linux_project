#include <stdio.h>

#include "ui.h"

static lv_obj_t *temperature_value;
static lv_obj_t *temperature_state;
static lv_obj_t *humidity_value;
static lv_obj_t *humidity_state;
static lv_obj_t *acceleration_value;
static lv_obj_t *vibration_value;
static lv_obj_t *vibration_state;
static lv_obj_t *motor_value;
static lv_obj_t *message_label;

static void set_state_label(lv_obj_t *label, int alarm)
{
	if (alarm)
	{
		lv_label_set_text(label, "ALARM");
		lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_RED), 0);
	}
	else
	{
		lv_label_set_text(label, "NORMAL");
		lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_GREEN), 0);
	}
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title,
			     lv_coord_t x, lv_coord_t y,
			     lv_coord_t width, lv_coord_t height)
{
	lv_obj_t *card;
	lv_obj_t *label;

	card = lv_obj_create(parent);
	lv_obj_set_pos(card, x, y);
	lv_obj_set_size(card, width, height);
	lv_obj_set_style_radius(card, 12, 0);
	lv_obj_set_style_border_width(card, 1, 0);
	lv_obj_set_style_border_color(card, lv_color_hex(0xcbd5e1), 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), 0);
	lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

	label = lv_label_create(card);
	lv_label_set_text(label, title);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(0x334155), 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
	return card;
}

static lv_obj_t *create_value_label(lv_obj_t *parent, const char *text)
{
	lv_obj_t *label;

	label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(0x0f172a), 0);
	lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 10);
	return label;
}

static lv_obj_t *create_state_label(lv_obj_t *parent)
{
	lv_obj_t *label;

	label = lv_label_create(parent);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
	lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
	set_state_label(label, 0);
	return label;
}

static void button_event_cb(lv_event_t *event)
{
	const char *name = lv_event_get_user_data(event);

	if (lv_event_get_code(event) != LV_EVENT_CLICKED)
		return;

	if (!name)
		return;

	lv_label_set_text_fmt(message_label, "%s button clicked", name);
}

static void create_button(lv_obj_t *parent, const char *text,
			  lv_coord_t x, lv_coord_t width)
{
	lv_obj_t *button;
	lv_obj_t *label;

	button = lv_btn_create(parent);
	lv_obj_set_pos(button, x, 0);
	lv_obj_set_size(button, width, 56);
	lv_obj_set_style_radius(button, 8, 0);
	lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED,
			    (void *)text);

	label = lv_label_create(button);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
	lv_obj_center(label);
}

void ui_create(void)
{
	lv_obj_t *screen;
	lv_obj_t *title;
	lv_obj_t *subtitle;
	lv_obj_t *temperature_card;
	lv_obj_t *humidity_card;
	lv_obj_t *acceleration_card;
	lv_obj_t *vibration_card;
	lv_obj_t *motor_card;
	lv_obj_t *button_area;

	screen = lv_scr_act();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0xf1f5f9), 0);
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

	title = lv_label_create(screen);
	lv_label_set_text(title, "HCM MONITORING SYSTEM");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(0x0f172a), 0);
	lv_obj_set_pos(title, 28, 18);

	subtitle = lv_label_create(screen);
	lv_label_set_text(subtitle, "i.MX6ULL Embedded Linux");
	lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(subtitle, lv_color_hex(0x64748b), 0);
	lv_obj_set_pos(subtitle, 684, 29);

	temperature_card = create_card(screen, "TEMPERATURE",
				       24, 72, 310, 150);
	temperature_value = create_value_label(temperature_card, "--.- C");
	temperature_state = create_state_label(temperature_card);

	humidity_card = create_card(screen, "HUMIDITY", 357, 72, 310, 150);
	humidity_value = create_value_label(humidity_card, "--.- %RH");
	humidity_state = create_state_label(humidity_card);

	vibration_card = create_card(screen, "VIBRATION", 690, 72, 310, 150);
	vibration_value = create_value_label(vibration_card, "---.- mg");
	vibration_state = create_state_label(vibration_card);

	acceleration_card = create_card(screen, "ACCELERATION",
				        24, 244, 643, 150);
	acceleration_value = create_value_label(acceleration_card,
					        "X: ----  Y: ----  Z: ---- g");

	motor_card = create_card(screen, "MOTOR", 690, 244, 310, 150);
	motor_value = create_value_label(motor_card, "CLOSED");

	button_area = lv_obj_create(screen);
	lv_obj_set_pos(button_area, 24, 416);
	lv_obj_set_size(button_area, 976, 82);
	lv_obj_set_style_bg_opa(button_area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(button_area, 0, 0);
	lv_obj_set_style_pad_all(button_area, 0, 0);
	lv_obj_clear_flag(button_area, LV_OBJ_FLAG_SCROLLABLE);

	create_button(button_area, "SETTINGS", 0, 280);
	create_button(button_area, "SAVE CONFIG", 348, 280);
	create_button(button_area, "MOTOR CONTROL", 696, 280);

	message_label = lv_label_create(screen);
	lv_label_set_text(message_label, "Touch a button to test the input device");
	lv_obj_set_style_text_font(message_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(message_label, lv_color_hex(0x475569), 0);
	lv_obj_align(message_label, LV_ALIGN_BOTTOM_MID, 0, -22);
}

void ui_set_temperature(float value, int alarm, int valid)
{
	char text[32];

	if (!valid)
	{
		lv_label_set_text(temperature_value, "--.- C");
		lv_label_set_text(temperature_state, "WAITING");
		return;
	}
	snprintf(text, sizeof(text), "%.1f C", value);
	lv_label_set_text(temperature_value, text);
	set_state_label(temperature_state, alarm);
}

void ui_set_humidity(float value, int alarm, int valid)
{
	char text[32];

	if (!valid)
	{
		lv_label_set_text(humidity_value, "--.- %RH");
		lv_label_set_text(humidity_state, "WAITING");
		return;
	}
	snprintf(text, sizeof(text), "%.1f %%RH", value);
	lv_label_set_text(humidity_value, text);
	set_state_label(humidity_state, alarm);
}

void ui_set_acceleration(float x, float y, float z, int valid)
{
	char text[64];

	if (!valid)
	{
		lv_label_set_text(acceleration_value,
				  "X: ----  Y: ----  Z: ---- g");
		return;
	}
	snprintf(text, sizeof(text), "X: %.3f  Y: %.3f  Z: %.3f g",
		 x, y, z);
	lv_label_set_text(acceleration_value, text);
}

void ui_set_vibration(float value, int alarm, int valid)
{
	char text[32];

	if (!valid)
	{
		lv_label_set_text(vibration_value, "---.- mg");
		lv_label_set_text(vibration_state, "WAITING");
		return;
	}
	snprintf(text, sizeof(text), "%.1f mg", value);
	lv_label_set_text(vibration_value, text);
	set_state_label(vibration_state, alarm);
}

void ui_set_motor_state(int open, int running)
{
	if (running)
		lv_label_set_text(motor_value, "RUNNING");
	else
		lv_label_set_text(motor_value, open ? "OPEN" : "CLOSED");
}
