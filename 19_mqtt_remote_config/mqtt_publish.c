#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mqttclient.h"
#include "mqtt_publish.h"

#define MQTT_DATA_TOPIC		"device/imx6ull/data"
#define MQTT_MOTOR_TOPIC	"device/imx6ull/control/motor"
#define MQTT_CONFIG_TOPIC	"device/imx6ull/control/config"
#define MQTT_CONFIG_RESULT_TOPIC "device/imx6ull/config/result"
#define MQTT_PUBLISH_PERIOD_MS	5000
#define MQTT_RETRY_PERIOD_MS	3000

static void mqtt_thread_sleep(struct mqtt_publish_context *context,
			      unsigned int milliseconds)
{
	unsigned int elapsed = 0;
	unsigned int delay;

	while (!*context->stop_requested && elapsed < milliseconds)
	{
		delay = milliseconds - elapsed;
		if (delay > 100)
			delay = 100;
		usleep(delay * 1000);
		elapsed += delay;
	}
}

static void motor_message_handler(void *client, message_data_t *message_data)
{
	mqtt_client_t *mqtt_client = client;
	struct mqtt_publish_context *context;
	char command[32];
	size_t length;

	context = mqtt_client->mqtt_reconnect_data;
	if (!context || !context->handle_motor_command ||
	    !message_data || !message_data->message)
		return;

	length = message_data->message->payloadlen;
	if (length >= sizeof(command))
		length = sizeof(command) - 1;

	memcpy(command, message_data->message->payload, length);
	command[length] = '\0';

	while (length > 0 &&
	       (command[length - 1] == '\r' || command[length - 1] == '\n' ||
		command[length - 1] == ' ' || command[length - 1] == '\t'))
	{
		command[length - 1] = '\0';
		length--;
	}

	printf("MQTT motor command: %s\n", command);
	context->handle_motor_command(command, context->user_data);
}

static void config_message_handler(void *client, message_data_t *message_data)
{
	mqtt_client_t *mqtt_client = client;
	struct mqtt_publish_context *context;
	mqtt_message_t result_message;
	char command[128];
	char result[96];
	char payload[160];
	size_t length;
	int ret;

	context = mqtt_client->mqtt_reconnect_data;
	if (!context || !context->handle_config_command ||
	    !message_data || !message_data->message)
		return;

	length = message_data->message->payloadlen;
	if (length >= sizeof(command))
		length = sizeof(command) - 1;
	memcpy(command, message_data->message->payload, length);
	command[length] = '\0';

	while (length > 0 &&
	       (command[length - 1] == '\r' || command[length - 1] == '\n' ||
		command[length - 1] == ' ' || command[length - 1] == '\t'))
	{
		command[length - 1] = '\0';
		length--;
	}

	printf("MQTT config command: %s\n", command);
	memset(result, 0, sizeof(result));
	ret = context->handle_config_command(command, result, sizeof(result),
					     context->user_data);
	snprintf(payload, sizeof(payload),
		 "{\"result\":\"%s\",\"message\":\"%s\"}",
		 ret ? "error" : "ok", result);

	memset(&result_message, 0, sizeof(result_message));
	result_message.qos = QOS0;
	result_message.payload = payload;
	result_message.payloadlen = strlen(payload);
	ret = mqtt_publish(mqtt_client, MQTT_CONFIG_RESULT_TOPIC,
			   &result_message);
	if (ret)
		fprintf(stderr, "MQTT config result publish failed: %d\n", ret);
}

void *mqtt_publish_thread(void *argument)
{
	struct mqtt_publish_context *context = argument;
	struct mqtt_system_data data;
	mqtt_client_t *client;
	mqtt_message_t message;
	char client_id[64];
	char payload[768];
	int connected_once = 0;
	int ret;

	mqtt_log_init();
	client = mqtt_lease();
	if (!client)
	{
		fprintf(stderr, "MQTT: allocate client failed\n");
		return NULL;
	}

	snprintf(client_id, sizeof(client_id), "imx6ull-hcm-%ld",
		 (long)getpid());
	mqtt_set_host(client, (char *)context->host);
	mqtt_set_port(client, (char *)context->port);
	mqtt_set_client_id(client, client_id);
	mqtt_set_clean_session(client, 1);
	mqtt_set_reconnect_data(client, context);

	while (!*context->stop_requested)
	{
		ret = mqtt_connect(client);
		if (!ret)
		{
			connected_once = 1;
			printf("MQTT connected to %s:%s\n",
			       context->host, context->port);
			ret = mqtt_subscribe(client, MQTT_MOTOR_TOPIC, QOS1,
					     motor_message_handler);
			if (ret)
				fprintf(stderr, "MQTT subscribe failed: %d\n", ret);
			else
				printf("MQTT subscribed: %s\n",
				       MQTT_MOTOR_TOPIC);

			ret = mqtt_subscribe(client, MQTT_CONFIG_TOPIC, QOS1,
					     config_message_handler);
			if (ret)
				fprintf(stderr, "MQTT config subscribe failed: %d\n",
					ret);
			else
				printf("MQTT subscribed: %s\n",
				       MQTT_CONFIG_TOPIC);
			break;
		}

		fprintf(stderr, "MQTT connect failed: %d, retrying...\n", ret);
		mqtt_thread_sleep(context, MQTT_RETRY_PERIOD_MS);
	}

	memset(&message, 0, sizeof(message));
	message.qos = QOS1;
	message.payload = payload;

	while (!*context->stop_requested && connected_once)
	{
		memset(&data, 0, sizeof(data));
		context->get_system_data(&data, context->user_data);

		snprintf(payload, sizeof(payload),
			 "{\"temperature\":%.1f,\"humidity\":%.1f,"
			 "\"acc_x\":%.3f,\"acc_y\":%.3f,\"acc_z\":%.3f,"
			 "\"vibration\":%.1f,"
			 "\"dht11_valid\":%d,\"adxl345_valid\":%d,"
			 "\"dht11_fault\":%d,\"adxl345_fault\":%d,"
			 "\"temperature_alarm\":%d,\"humidity_alarm\":%d,"
			 "\"vibration_alarm\":%d,"
			 "\"motor_open\":%d,\"motor_running\":%d,"
			 "\"motor_mode\":\"%s\","
			 "\"temperature_limit\":%d,"
			 "\"humidity_limit\":%u,"
			 "\"vibration_limit\":%u,"
			 "\"dht11_period\":%u,\"adxl345_period\":%u,"
			 "\"config_save_status\":%d}",
			 data.temperature_c, data.humidity_percent,
			 data.acc_x_g, data.acc_y_g, data.acc_z_g,
			 data.vibration_mg,
			 data.dht11_valid, data.adxl345_valid,
			 data.dht11_fault, data.adxl345_fault,
			 data.temperature_alarm, data.humidity_alarm,
			 data.vibration_alarm, data.motor_open,
			 data.motor_running,
			 data.motor_auto_mode ? "auto" : "manual",
			 data.temperature_limit_c,
			 data.humidity_limit_percent,
			 data.vibration_limit_mg,
			 data.dht11_period_ms,
			 data.adxl345_period_ms,
			 data.config_save_status);

		message.payloadlen = strlen(payload);
		ret = mqtt_publish(client, MQTT_DATA_TOPIC, &message);
		if (ret)
			fprintf(stderr, "MQTT publish failed: %d\n", ret);
		else
			printf("MQTT publish: %s\n", payload);

		mqtt_thread_sleep(context, MQTT_PUBLISH_PERIOD_MS);
	}

	if (connected_once)
	{
		mqtt_disconnect(client);
		mqtt_release(client);
	}

	printf("MQTT thread exited.\n");
	return NULL;
}
