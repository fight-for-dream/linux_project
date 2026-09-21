#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mqttclient.h"
#include "mqtt_publish.h"

#define MQTT_TOPIC		"device/imx6ull/data"
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

void *mqtt_publish_thread(void *argument)
{
	struct mqtt_publish_context *context = argument;
	struct mqtt_system_data data;
	mqtt_client_t *client;
	mqtt_message_t message;
	char client_id[64];
	char payload[512];
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

	while (!*context->stop_requested)
	{
		ret = mqtt_connect(client);
		if (!ret)
		{
			connected_once = 1;
			printf("MQTT connected to %s:%s\n",
			       context->host, context->port);
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
			 "\"motor_mode\":\"%s\"}",
			 data.temperature_c, data.humidity_percent,
			 data.acc_x_g, data.acc_y_g, data.acc_z_g,
			 data.vibration_mg,
			 data.dht11_valid, data.adxl345_valid,
			 data.dht11_fault, data.adxl345_fault,
			 data.temperature_alarm, data.humidity_alarm,
			 data.vibration_alarm, data.motor_open,
			 data.motor_running,
			 data.motor_auto_mode ? "auto" : "manual");

		message.payloadlen = strlen(payload);
		ret = mqtt_publish(client, MQTT_TOPIC, &message);
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
