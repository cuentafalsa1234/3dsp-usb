/*
 * 3dsp_plug - minimal userspace replacement for the original
 * "3DSP USB Manager" app. Sends IOCTL_BUSENUM_PLUGIN_HARDWARE to the
 * 3dspusbbus char device so the bus starts the BT/WLAN children.
 *
 * Usage: 3dsp_plug [bt] [wlan]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#define BUS_IOCTL_MAGIC		0x6b
#define BUSENUM_IOCTL(_i_)	_IO(BUS_IOCTL_MAGIC, _i_)
#define IOCTL_BUSENUM_PLUGIN_HARDWARE	BUSENUM_IOCTL(0x0)
#define IOCTL_BUSENUM_UNPLUG_HARDWARE	BUSENUM_IOCTL(0x1)
#define IOCTL_BUSENUM_POWER_SOURCE_AC	BUSENUM_IOCTL(0x33)
#define IOCTL_BUSENUM_QUERY_DEVICE_STATE	BUSENUM_IOCTL(0x20)

struct busenum_plugin {
	unsigned long size;
	unsigned long serial;
	unsigned char hwids[60];
};

static int do_plug(int fd, unsigned long serial, const char *hwid)
{
	struct busenum_plugin p;
	memset(&p, 0, sizeof(p));
	p.size = (unsigned long)((char *)p.hwids - (char *)&p) + strlen(hwid) + 1;
	p.serial = serial;
	strcpy((char *)p.hwids, hwid);

	if (ioctl(fd, IOCTL_BUSENUM_PLUGIN_HARDWARE, &p) < 0) {
		fprintf(stderr, "plug serial=%lu failed: %s\n", serial, strerror(errno));
		return -1;
	}
	printf("plug serial=%lu (%s) OK\n", serial, hwid);
	return 0;
}

int main(int argc, char **argv)
{
	int fd, want_bt = 0, want_wlan = 0, i;

	if (argc < 2)
		want_bt = want_wlan = 1;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "bt"))   want_bt = 1;
		if (!strcmp(argv[i], "wlan")) want_wlan = 1;
	}

	fd = open("/dev/tdspbus", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open /dev/tdspbus: %s (crea el nodo con mknod)\n", strerror(errno));
		return 1;
	}

	if (want_bt)
		if (do_plug(fd, 1, "usb\\vid_05e1&pid_0100&mi_00") < 0)
			return 1;
	if (want_wlan)
		if (do_plug(fd, 2, "usb\\vid_05e1&pid_0100&mi_01") < 0)
			return 1;

	/* notify AC power source */
	ioctl(fd, IOCTL_BUSENUM_POWER_SOURCE_AC, NULL);
	close(fd);
	return 0;
}
