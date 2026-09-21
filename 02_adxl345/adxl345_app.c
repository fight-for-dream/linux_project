/*
 * Build:
 * arm-buildroot-linux-gnueabihf-gcc -Wall -O2 adxl345_app.c -o adxl345_test
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

struct adxl345_sample
{
	int16_t x;
	int16_t y;
	int16_t z;
};

int main(void)
{
	struct adxl345_sample sample;
	float x_g, y_g, z_g;
	ssize_t len;
	int fd;

	fd = open("/dev/hcm_adxl345", O_RDONLY);
	if (fd < 0)
	{
		perror("open");
		return 1;
	}

	while (1)
	{
		len = read(fd, &sample, sizeof(sample));
		if (len == (ssize_t)sizeof(sample))
		{
			x_g = sample.x * 0.0039f;
			y_g = sample.y * 0.0039f;
			z_g = sample.z * 0.0039f;
			printf("raw: x=%6d y=%6d z=%6d, "
			       "g: x=%7.3f y=%7.3f z=%7.3f\n",
			       sample.x, sample.y, sample.z,
			       x_g, y_g, z_g);
			fflush(stdout);
		}
		else if (len < 0)
		{
			perror("ADXL345 read");
			break;
		}
		else
		{
			fprintf(stderr, "Unexpected data length: %ld\n", (long)len);
			break;
		}

		usleep(200000);
	}

	close(fd);
	return 0;
}
