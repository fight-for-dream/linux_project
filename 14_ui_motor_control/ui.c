#include <stdio.h>
#include <string.h>

#include "ui.h"

#define UI_DISPLAY_WIDTH	1024
#define UI_DISPLAY_HEIGHT	600

static lv_obj_t *temperature_value;
static lv_obj_t *temperature_state;
static lv_obj_t *humidity_value;
static lv_obj_t *humidity_state;
static lv_obj_t *acceleration_value;
static lv_obj_t *vibration_value;
static lv_obj_t *vibration_state;
static lv_obj_t *motor_value;
static lv_obj_t *message_label;
static lv_obj_t *settings_window;
static lv_obj_t *motor_window;
static lv_obj_t *motor_mode_switch;
static lv_obj_t *motor_open_button;
static lv_obj_t *motor_close_button;
static lv_obj_t *motor_dialog_state;
static lv_obj_t *temperature_spinbox;
static lv_obj_t *humidity_spinbox;
static lv_obj_t *vibration_spinbox;
static lv_obj_t *dht_period_spinbox;
static lv_obj_t *adxl_period_spinbox;
static struct ui_operations ui_ops;
static void *ui_user_data;

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

static lv_obj_t *create_value_label(lv_obj_t *parent, const char *text, unsigned int size)
{
	lv_obj_t *label;

	label = lv_label_create(parent);
	lv_label_set_text(label, text);
	if(size == 40)
		lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
	else
		lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
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

static void spinbox_minus_cb(lv_event_t *event)
{
	lv_obj_t *spinbox = lv_event_get_user_data(event);

	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		lv_spinbox_decrement(spinbox);
}

static void spinbox_plus_cb(lv_event_t *event)
{
	lv_obj_t *spinbox = lv_event_get_user_data(event);

	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		lv_spinbox_increment(spinbox);
}

static void close_settings_window(void)
{
	if (settings_window)
	{
		lv_obj_del(settings_window);
		settings_window = NULL;
	}
}

static void settings_cancel_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		close_settings_window();
}

static void settings_apply_cb(lv_event_t *event)
{
	struct system_config config;

	if (lv_event_get_code(event) != LV_EVENT_CLICKED)
		return;
	if (!ui_ops.get_config || !ui_ops.apply_config)
		return;

	ui_ops.get_config(&config, ui_user_data);
	config.temperature_limit_c =
		lv_spinbox_get_value(temperature_spinbox);
	config.humidity_limit_percent =
		lv_spinbox_get_value(humidity_spinbox);
	config.vibration_limit_mg =
		lv_spinbox_get_value(vibration_spinbox);
	config.dht11_period_ms = lv_spinbox_get_value(dht_period_spinbox);
	config.adxl345_period_ms = lv_spinbox_get_value(adxl_period_spinbox);
	ui_ops.apply_config(&config, ui_user_data);

	lv_label_set_text(message_label,
			  "Configuration applied (not saved to EEPROM)");
	close_settings_window();
}

static lv_obj_t *create_small_button(lv_obj_t *parent, const char *text,
				     lv_coord_t x, lv_coord_t y,
				     lv_event_cb_t callback,
				     void *user_data)
{
	lv_obj_t *button;
	lv_obj_t *label;

	button = lv_btn_create(parent);
	lv_obj_set_pos(button, x, y);
	lv_obj_set_size(button, 54, 42);
	lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
	label = lv_label_create(button);
	lv_label_set_text(label, text);
	lv_obj_center(label);
	return button;
}

static lv_obj_t *create_config_row(lv_obj_t *parent, const char *name,
				   const char *unit, lv_coord_t y,
				   int32_t minimum, int32_t maximum,
				   uint32_t step, uint8_t digits,
				   int32_t value)
{
	lv_obj_t *label;
	lv_obj_t *spinbox;

	label = lv_label_create(parent);
	lv_label_set_text(label, name);
	lv_obj_set_pos(label, 30, y + 12);

	spinbox = lv_spinbox_create(parent);
	lv_obj_set_pos(spinbox, 280, y);
	lv_obj_set_size(spinbox, 170, 42);
	lv_spinbox_set_range(spinbox, minimum, maximum);
	lv_spinbox_set_digit_format(spinbox, digits, 0);
	lv_spinbox_set_step(spinbox, step);
	lv_spinbox_set_value(spinbox, value);

	create_small_button(parent, "-", 214, y, spinbox_minus_cb, spinbox);
	create_small_button(parent, "+", 462, y, spinbox_plus_cb, spinbox);

	label = lv_label_create(parent);
	lv_label_set_text(label, unit);
	lv_obj_set_pos(label, 532, y + 12);
	return spinbox;
}

static void open_settings_window(void)
{
	struct system_config config;
	lv_obj_t *panel;
	lv_obj_t *label;
	lv_obj_t *button;

	if (settings_window || !ui_ops.get_config)
		return;
	ui_ops.get_config(&config, ui_user_data);

	settings_window = lv_obj_create(lv_layer_top());
	lv_obj_set_size(settings_window, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT);
	lv_obj_set_pos(settings_window, 0, 0);
	lv_obj_set_style_bg_color(settings_window, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(settings_window, LV_OPA_50, 0);
	lv_obj_set_style_border_width(settings_window, 0, 0);
	lv_obj_set_style_pad_all(settings_window, 0, 0);
	lv_obj_clear_flag(settings_window, LV_OBJ_FLAG_SCROLLABLE);

	panel = lv_obj_create(settings_window);
	lv_obj_set_size(panel, 650, 520);
	lv_obj_center(panel);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	label = lv_label_create(panel);
	lv_label_set_text(label, "SYSTEM SETTINGS");
	lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
	lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 4);

	temperature_spinbox = create_config_row(panel, "Temperature limit",
		"C", 55, -40, 80, 1, 3, config.temperature_limit_c);
	humidity_spinbox = create_config_row(panel, "Humidity limit",
		"%RH", 125, 1, 100, 1, 3, config.humidity_limit_percent);
	vibration_spinbox = create_config_row(panel, "Vibration limit",
		"mg", 195, 1, 16000, 10, 5, config.vibration_limit_mg);
	dht_period_spinbox = create_config_row(panel, "DHT11 period",
		"ms", 265, 2000, 60000, 1000, 5, config.dht11_period_ms);
	adxl_period_spinbox = create_config_row(panel, "ADXL345 period",
		"ms", 335, 10, 10000, 100, 5, config.adxl345_period_ms);

	button = lv_btn_create(panel);
	lv_obj_set_pos(button, 118, 420);
	lv_obj_set_size(button, 170, 52);
	lv_obj_add_event_cb(button, settings_cancel_cb, LV_EVENT_CLICKED, NULL);
	label = lv_label_create(button);
	lv_label_set_text(label, "CANCEL");
	lv_obj_center(label);

	button = lv_btn_create(panel);
	lv_obj_set_pos(button, 342, 420);
	lv_obj_set_size(button, 170, 52);
	lv_obj_add_event_cb(button, settings_apply_cb, LV_EVENT_CLICKED, NULL);
	label = lv_label_create(button);
	lv_label_set_text(label, "APPLY");
	lv_obj_center(label);
}

static void settings_button_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		open_settings_window();
}

static void save_button_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED)
		return;
	if (!ui_ops.request_save)
	{
		lv_label_set_text(message_label, "Save function is unavailable");
		return;
	}

	ui_ops.request_save(ui_user_data);
	lv_label_set_text(message_label, "Saving configuration...");
}

static void close_motor_window(void)
{
	if (motor_window)
	{
		lv_obj_del(motor_window);
		motor_window = NULL;
		motor_dialog_state = NULL;
	}
}

static void motor_close_window_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		close_motor_window();
}

static void update_manual_button_state(int auto_mode)
{
	if (auto_mode)
	{
		lv_obj_add_state(motor_open_button, LV_STATE_DISABLED);
		lv_obj_add_state(motor_close_button, LV_STATE_DISABLED);
	}
	else
	{
		lv_obj_clear_state(motor_open_button, LV_STATE_DISABLED);
		lv_obj_clear_state(motor_close_button, LV_STATE_DISABLED);
	}
}

static void motor_mode_changed_cb(lv_event_t *event)
{
	int auto_mode;

	if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
		return;
	auto_mode = lv_obj_has_state(motor_mode_switch, LV_STATE_CHECKED);
	if (ui_ops.set_motor_mode)
		ui_ops.set_motor_mode(auto_mode, ui_user_data);
	update_manual_button_state(auto_mode);
	lv_label_set_text(motor_dialog_state,
			  auto_mode ? "AUTO mode: alarm control enabled"
				    : "MANUAL mode: use OPEN/CLOSE");
}

static void motor_open_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED)
		return;
	if (ui_ops.request_motor)
		ui_ops.request_motor(1, ui_user_data);
	lv_label_set_text(motor_dialog_state, "Manual OPEN requested");
}

static void motor_close_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) != LV_EVENT_CLICKED)
		return;
	if (ui_ops.request_motor)
		ui_ops.request_motor(0, ui_user_data);
	lv_label_set_text(motor_dialog_state, "Manual CLOSE requested");
}

static void open_motor_window(void)
{
	lv_obj_t *panel;
	lv_obj_t *label;
	lv_obj_t *button;
	int auto_mode;
	int open;
	int running;

	if (motor_window || !ui_ops.get_motor_control)
		return;
	ui_ops.get_motor_control(&auto_mode, &open, &running, ui_user_data);

	motor_window = lv_obj_create(lv_layer_top());
	lv_obj_set_size(motor_window, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT);
	lv_obj_set_pos(motor_window, 0, 0);
	lv_obj_set_style_bg_color(motor_window, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(motor_window, LV_OPA_50, 0);
	lv_obj_set_style_border_width(motor_window, 0, 0);
	lv_obj_set_style_pad_all(motor_window, 0, 0);
	lv_obj_clear_flag(motor_window, LV_OBJ_FLAG_SCROLLABLE);

	panel = lv_obj_create(motor_window);
	lv_obj_set_size(panel, 620, 380);
	lv_obj_center(panel);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	label = lv_label_create(panel);
	lv_label_set_text(label, "MOTOR CONTROL");
	lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
	lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);

	label = lv_label_create(panel);
	lv_label_set_text(label, "MANUAL");
	lv_obj_set_pos(label, 170, 78);
	motor_mode_switch = lv_switch_create(panel);
	lv_obj_set_pos(motor_mode_switch, 270, 65);
	lv_obj_set_size(motor_mode_switch, 80, 42);
	if (auto_mode)
		lv_obj_add_state(motor_mode_switch, LV_STATE_CHECKED);
	lv_obj_add_event_cb(motor_mode_switch, motor_mode_changed_cb,
			    LV_EVENT_VALUE_CHANGED, NULL);
	label = lv_label_create(panel);
	lv_label_set_text(label, "AUTO");
	lv_obj_set_pos(label, 375, 78);

	motor_open_button = lv_btn_create(panel);
	lv_obj_set_pos(motor_open_button, 90, 145);
	lv_obj_set_size(motor_open_button, 180, 62);
	lv_obj_add_event_cb(motor_open_button, motor_open_cb,
			    LV_EVENT_CLICKED, NULL);
	label = lv_label_create(motor_open_button);
	lv_label_set_text(label, "OPEN");
	lv_obj_center(label);

	motor_close_button = lv_btn_create(panel);
	lv_obj_set_pos(motor_close_button, 330, 145);
	lv_obj_set_size(motor_close_button, 180, 62);
	lv_obj_add_event_cb(motor_close_button, motor_close_cb,
			    LV_EVENT_CLICKED, NULL);
	label = lv_label_create(motor_close_button);
	lv_label_set_text(label, "CLOSE");
	lv_obj_center(label);
	update_manual_button_state(auto_mode);

	motor_dialog_state = lv_label_create(panel);
	if (running)
		lv_label_set_text(motor_dialog_state, "Motor is running");
	else if (auto_mode)
		lv_label_set_text(motor_dialog_state,
				  open ? "AUTO mode: motor open"
				       : "AUTO mode: motor closed");
	else
		lv_label_set_text(motor_dialog_state,
				  open ? "MANUAL mode: motor open"
				       : "MANUAL mode: motor closed");
	lv_obj_align(motor_dialog_state, LV_ALIGN_BOTTOM_MID, 0, -72);

	button = lv_btn_create(panel);
	lv_obj_set_size(button, 180, 50);
	lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -8);
	lv_obj_add_event_cb(button, motor_close_window_cb,
			    LV_EVENT_CLICKED, NULL);
	label = lv_label_create(button);
	lv_label_set_text(label, "BACK");
	lv_obj_center(label);
}

static void motor_button_cb(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
		open_motor_window();
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
	if (!strcmp(text, "SETTINGS"))
		lv_obj_add_event_cb(button, settings_button_cb,
				    LV_EVENT_CLICKED, NULL);
	else if (!strcmp(text, "SAVE CONFIG"))
		lv_obj_add_event_cb(button, save_button_cb,
				    LV_EVENT_CLICKED, NULL);
	else if (!strcmp(text, "MOTOR CONTROL"))
		lv_obj_add_event_cb(button, motor_button_cb,
				    LV_EVENT_CLICKED, NULL);
	else
		lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED,
				    (void *)text);

	label = lv_label_create(button);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
	lv_obj_center(label);
}

void ui_create(const struct ui_operations *operations, void *user_data)
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

	memset(&ui_ops, 0, sizeof(ui_ops));
	if (operations)
		ui_ops = *operations;
	ui_user_data = user_data;

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
	temperature_value = create_value_label(temperature_card, "--.- C", 40);
	temperature_state = create_state_label(temperature_card);

	humidity_card = create_card(screen, "HUMIDITY", 357, 72, 310, 150);
	humidity_value = create_value_label(humidity_card, "--.- %RH", 40);
	humidity_state = create_state_label(humidity_card);

	vibration_card = create_card(screen, "VIBRATION", 690, 72, 310, 150);
	vibration_value = create_value_label(vibration_card, "---.- mg", 40);
	vibration_state = create_state_label(vibration_card);

	acceleration_card = create_card(screen, "ACCELERATION",
				        24, 244, 643, 150);
	acceleration_value = create_value_label(acceleration_card,
					        "X: ----  Y: ----  Z: ---- g", 40);

	motor_card = create_card(screen, "MOTOR", 690, 244, 310, 150);
	motor_value = create_value_label(motor_card, "CLOSED", 14);

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

void ui_set_motor_state(int open, int running, int auto_mode)
{
	char text[32];

	if (running)
		snprintf(text, sizeof(text), "RUNNING (%s)",
			 auto_mode ? "AUTO" : "MANUAL");
	else
		snprintf(text, sizeof(text), "%s (%s)", open ? "OPEN" : "CLOSED",
			 auto_mode ? "AUTO" : "MANUAL");
	lv_label_set_text(motor_value, text);

	if (motor_window && motor_dialog_state)
	{
		if (running)
			lv_label_set_text(motor_dialog_state, "Motor is running");
		else if (auto_mode)
			lv_label_set_text(motor_dialog_state,
					  open ? "AUTO mode: motor open"
					       : "AUTO mode: motor closed");
		else
			lv_label_set_text(motor_dialog_state,
					  open ? "MANUAL mode: motor open"
					       : "MANUAL mode: motor closed");
	}
}

void ui_set_save_status(int status)
{
	static int previous_status;

	if (status == previous_status)
		return;
	previous_status = status;

	if (status > 1)
		lv_label_set_text(message_label,
				  "Configuration saved to EEPROM");
	else if (status < 0)
		lv_label_set_text(message_label,
				  "Failed to save configuration");
	else if (status == 1)
		lv_label_set_text(message_label, "Saving configuration...");
}
