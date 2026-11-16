/* Test raw vendor control transfer to the 3DSP combo via usbfs */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

int main(int argc, char **argv)
{
	const char *devpath = (argc > 1) ? argv[1] : "/dev/bus/usb/001/005";
	struct usbdevfs_ctrltransfer ct;
	int fd, ret;
	unsigned char buf[4] = {0};

	fd = open(devpath, O_RDWR);
	if (fd < 0) { perror("open"); return 1; }

	/* generic vendor IN request: bRequest=0x01 (whatever), mimic a reg read */
	memset(&ct, 0, sizeof(ct));
	ct.bRequestType = 0xC0;		/* DIR_IN | TYPE_VENDOR | RECIP_DEVICE */
	ct.bRequest = 0x01;
	ct.wValue = 0x0000;
	ct.wIndex = 0x0000;
	ct.wLength = 4;
	ct.data = buf;
	ct.timeout = 5000;		/* ms */

	ret = ioctl(fd, USBDEVFS_CONTROL, &ct);
	if (ret < 0) {
		printf("control IN: error %d (%s)\n", errno, strerror(errno));
	} else {
		printf("control IN: OK, %d bytes: %02x %02x %02x %02x\n",
			ret, buf[0], buf[1], buf[2], buf[3]);
	}

	/* device descriptor read (standard request) to verify ep0 is alive */
	memset(&ct, 0, sizeof(ct));
	ct.bRequestType = 0x80;
	ct.bRequest = 0x06;		/* GET_DESCRIPTOR device */
	ct.wValue = 0x0100;
	ct.wLength = 18;
	ct.data = buf;
	ct.timeout = 5000;

	ret = ioctl(fd, USBDEVFS_CONTROL, &ct);
	if (ret < 0)
		printf("GET_DESC: error %d (%s)\n", errno, strerror(errno));
	else
		printf("GET_DESC: OK, %d bytes\n", ret);

	close(fd);
	return 0;
}
