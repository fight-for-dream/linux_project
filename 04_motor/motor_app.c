/*
 * Build:
 * arm-buildroot-linux-gnueabihf-gcc -Wall -O2 motor_app.c -o motor_test
 *
 * Test:
 * ./motor_test forward 512 3000
 * ./motor_test reverse 512 3000
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MOTOR_MAX_STEPS		8192
#define MOTOR_MIN_INTERVAL_US	500
#define MOTOR_MAX_INTERVAL_US	100000

struct motor_command
{
	int32_t direction;
	uint32_t steps;
	uint32_t interval_us;
};

static int string_to_uint(const char *string, unsigned int *value)
{
	char *end;
	unsigned long number;

	errno = 0;
	number = strtoul(string, &end, 0);
	if (errno || *string == '\0' || *end != '\0' || number > 0xffffffffUL)
		return -1;

	*value = (unsigned int)number;
	return 0;
}

int main(int argc, char *argv[])
{
	struct motor_command command;
	ssize_t ret;
	int fd;

	if (argc != 4)
	{
		fprintf(stderr,
			"Usage: %s <forward|reverse> <steps> <interval_us>\n",
			argv[0]);
		return 1;
	}

	if (!strcmp(argv[1], "forward"))
		command.direction = 1;
	else if (!strcmp(argv[1], "reverse"))
		command.direction = -1;
	else
	{
		fprintf(stderr, "Direction must be forward or reverse.\n");
		return 1;
	}

	if (string_to_uint(argv[2], &command.steps) ||
	    command.steps == 0 || command.steps > MOTOR_MAX_STEPS)
	{
		fprintf(stderr, "Steps must be in the range 1 to %u.\n",
			MOTOR_MAX_STEPS);
		return 1;
	}

	if (string_to_uint(argv[3], &command.interval_us) ||
	    command.interval_us < MOTOR_MIN_INTERVAL_US ||
	    command.interval_us > MOTOR_MAX_INTERVAL_US)
	{
		fprintf(stderr, "Interval must be in the range %u to %u us.\n",
			MOTOR_MIN_INTERVAL_US, MOTOR_MAX_INTERVAL_US);
		return 1;
	}

	fd = open("/dev/hcm_motor", O_WRONLY);
	if (fd < 0)
	{
		perror("open");
		return 1;
	}

	ret = write(fd, &command, sizeof(command));
	if (ret != (ssize_t)sizeof(command))
	{
		if (ret < 0)
			perror("motor write");
		else
			fprintf(stderr, "Unexpected write length: %ld\n", (long)ret);
		close(fd);
		return 1;
	}

	printf("Motor command completed: direction=%s, steps=%u, interval=%u us\n",
	       argv[1], command.steps, command.interval_us);
	close(fd);
	return 0;
}
