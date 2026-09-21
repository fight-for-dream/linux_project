#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mqtt_log.h"
#include "mqttclient.h"

#define MQTT_DEFAULT_PORT	"1883"
#define MQTT_TEST_TOPIC		"device/imx6ull/test"
#define MQTT_PUBLISH_PERIOD_MS	5000
#define MQTT_CONNECT_RETRY_MS	3000

static volatile sig_atomic_t stop_requested;

static void signal_handler(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static void sleep_ms(unsigned int milliseconds)
{
	struct timespec request;
	struct timespec remaining;

	request.tv_sec = milliseconds / 1000;
	request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (!stop_requested && nanosleep(&request, &remaining) < 0)
	{
		if (errno != EINTR)
			break;
		request = remaining;
	}
}

int main(int argc, char **argv)
{
	const char *broker_port = MQTT_DEFAULT_PORT;
	const char *broker_host;
	mqtt_client_t *client;
	mqtt_message_t message;
	char client_id[64];
	char payload[256];
	unsigned int counter = 0;
	int connected_once = 0;
	int ret;

	if (argc != 2 && argc != 3)
	{
		fprintf(stderr, "Usage: %s <broker_ip> [port]\n", argv[0]);
		return 1;
	}

	broker_host = argv[1];
	if (argc == 3)
		broker_port = argv[2];

	if (signal(SIGINT, signal_handler) == SIG_ERR ||
	    signal(SIGTERM, signal_handler) == SIG_ERR)
	{
		perror("signal");
		return 1;
	}

	mqtt_log_init();
	client = mqtt_lease();
	if (!client)
	{
		fprintf(stderr, "mqtt_lease failed\n");
		return 1;
	}

	snprintf(client_id, sizeof(client_id), "imx6ull-test-%ld",
		 (long)getpid());
	mqtt_set_host(client, (char *)broker_host);
	mqtt_set_port(client, (char *)broker_port);
	mqtt_set_client_id(client, client_id);
	mqtt_set_clean_session(client, 1);
	mqtt_set_keep_alive_interval(client, 30);
	mqtt_set_reconnect_try_duration(client, MQTT_CONNECT_RETRY_MS);

	printf("Broker   : %s:%s\n", broker_host, broker_port);
	printf("Client ID: %s\n", client_id);
	printf("Topic    : %s\n", MQTT_TEST_TOPIC);

	while (!stop_requested)
	{
		ret = mqtt_connect(client);
		if (!ret)
		{
			connected_once = 1;
			printf("MQTT connected.\n");
			break;
		}

		fprintf(stderr, "MQTT connect failed: %d; retry in %d ms\n",
			ret, MQTT_CONNECT_RETRY_MS);
		sleep_ms(MQTT_CONNECT_RETRY_MS);
	}

	memset(&message, 0, sizeof(message));
	message.qos = QOS1;
	message.retained = 0;
	message.payload = payload;

	while (!stop_requested && connected_once)
	{
		snprintf(payload, sizeof(payload),
			 "{\"device\":\"imx6ull\",\"counter\":%u,"
			 "\"message\":\"mqtt test\"}", counter++);
		message.payloadlen = strlen(payload);

		ret = mqtt_publish(client, MQTT_TEST_TOPIC, &message);
		if (!ret)
			printf("Published: %s\n", payload);
		else
			fprintf(stderr,
				"Publish failed: %d; waiting for reconnect\n", ret);

		sleep_ms(MQTT_PUBLISH_PERIOD_MS);
	}

	printf("Stopping MQTT client...\n");
	if (connected_once)
	{
		mqtt_disconnect(client);
		ret = mqtt_release(client);
		if (ret)
			fprintf(stderr, "mqtt_release failed: %d\n", ret);
	}
	printf("MQTT publish test exited.\n");
	return 0;
}
