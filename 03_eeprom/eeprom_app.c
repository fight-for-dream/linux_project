/*
 * Build:
 * arm-buildroot-linux-gnueabihf-gcc -Wall -O2 eeprom_app.c -o eeprom_test
 *
 * Test:
 * ./eeprom_test write "100ask EEPROM test"
 * ./eeprom_test read
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EEPROM_TEST_ADDRESS	0x80
#define EEPROM_TEST_SIZE	64

static int seek_test_area(int fd)
{
	if (lseek(fd, EEPROM_TEST_ADDRESS, SEEK_SET) < 0)
	{
		perror("lseek");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char data[EEPROM_TEST_SIZE] = { 0 };
	size_t length;
	ssize_t ret;
	int fd;

	if (argc < 2 || (strcmp(argv[1], "read") && strcmp(argv[1], "write")))
	{
		fprintf(stderr, "Usage:\n");
		fprintf(stderr, "  %s write <string>\n", argv[0]);
		fprintf(stderr, "  %s read\n", argv[0]);
		return 1;
	}

	fd = open("/dev/hcm_eeprom", O_RDWR);
	if (fd < 0)
	{
		perror("open");
		return 1;
	}

	if (seek_test_area(fd))
		goto err_close;

	if (!strcmp(argv[1], "write"))
	{
		if (argc != 3)
		{
			fprintf(stderr, "Please provide one string to write.\n");
			goto err_close;
		}

		length = strlen(argv[2]) + 1;
		if (length > sizeof(data))
		{
			fprintf(stderr, "String is too long; maximum is %u characters.\n",
				(unsigned int)sizeof(data) - 1);
			goto err_close;
		}

		ret = write(fd, argv[2], length);
		if (ret != (ssize_t)length)
		{
			if (ret < 0)
				perror("write");
			else
				fprintf(stderr, "Only wrote %ld of %u bytes.\n",
					(long)ret, (unsigned int)length);
			goto err_close;
		}
		printf("Wrote %u bytes at EEPROM address 0x%02x.\n",
		       (unsigned int)length, EEPROM_TEST_ADDRESS);
	}
	else
	{
		ret = read(fd, data, sizeof(data) - 1);
		if (ret < 0)
		{
			perror("read");
			goto err_close;
		}
		data[ret] = '\0';
		printf("Read from EEPROM address 0x%02x: %s\n",
		       EEPROM_TEST_ADDRESS, data);
	}

	close(fd);
	return 0;

err_close:
	close(fd);
	return 1;
}
