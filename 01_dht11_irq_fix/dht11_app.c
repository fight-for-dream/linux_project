/* Ubuntu 编译：arm-buildroot-linux-gnueabihf-gcc -Wall -O2 dht11_app.c -o dht11_app
 * 运行：./dht11_app；Ctrl+C 退出后再卸载驱动。
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/* 与驱动保持一致：2 字节，第 0 字节温度，第 1 字节湿度。 */
struct dht11_sample
{
	unsigned char temperature;
	unsigned char humidity;
};

int main(void)
{
	struct dht11_sample sample;
	int fd;
	ssize_t len;

	fd = open("/dev/hcm_dht11", O_RDONLY);
	if (fd < 0)
	{
		perror("open");
		return 1;
	}

	/* Ctrl+C 使用系统默认行为退出，内核自动关闭文件描述符。 */
	while (1)
	{
		len = read(fd, &sample, sizeof(sample));
		if (len == (ssize_t)sizeof(sample))
		{
			printf("temperature=%u C, humidity=%u %%RH\n",
			       (unsigned int)sample.temperature,
			       (unsigned int)sample.humidity);
			fflush(stdout);
		}
		else if (len < 0)
		{
			if (errno == EAGAIN)
				printf("Please wait for the next sample.\n");
			else if (errno == ETIMEDOUT || errno == EIO)
				perror("DHT11 read");
			else
			{
				perror("read");
				break;
			}
		}
		else
		{
			fprintf(stderr, "Unexpected data length: %ld\n", (long)len);
			break;
		}
		sleep(2);
	}

	close(fd);
	return 1;
}
