#include <ctype.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int do_exit = 0;

static void ctrlc_handler(int signo)
{
	printf("<ctrl-c>\r\n");
	do_exit = 1;
}

struct input_event const sync_event = {
	{0},	/* time */
	EV_SYN, /* type */
	0,      /* code */
	0       /* value */
};

static int send_key(int fd, unsigned key)
{
	struct input_event ev;
	int count;
	int ret = 0;

	ev.type = EV_KEY;
	ev.code = key;
	ev.value = 1; /* key down */

	count = write(fd, &ev, sizeof(ev));
	if (count != sizeof(ev)) {
		perror("write(input_event)");
		ret = errno;
	}
	count = write(fd, &sync_event, sizeof(sync_event));
	if (count != sizeof(ev)) {
		perror("write(sync)");
		ret = errno;
	}

	ev.value = 0; /* key down */
	count = write(fd, &ev, sizeof(ev));
	if (count != sizeof(ev)) {
		perror("write(input_event)");
		ret = errno;
	}
	count = write(fd, &sync_event, sizeof(sync_event));
	if (count != sizeof(ev)) {
		perror("write(sync)");
		ret = errno;
	}

	return ret;
}

/*
 * remove trailing whitespace and return new length
 */
static int trim(char *s, int len)
{
	s[len] = 0; /* force NULL termination */
	while (len) {
		--len;
		if (isspace(s[len])) {
			s[len] = 0;
		} else {
			++len;
			break;
		}
	}
	return len;
}

int main(int argc, char const * const argv[])
{
	struct uinput_user_dev uidev;
	char const *pipename;
	unsigned key;
	int fdpipe;
	int fdui;
	int res;

	if (1 < argc)
		pipename = argv[1];
	else
		pipename = "/tmp/kbdpipe";

	res = mkfifo(pipename, 0666);
	if ((0 > res) && (EEXIST != errno)) {
		fprintf(stderr, "%s:%d:%m", pipename, errno);
		return -1;
	}

	fdui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fdui < 0) {
		perror("/dev/uinput");
		return -1;
	}

	if (0 != ioctl(fdui, UI_SET_EVBIT, EV_KEY)) {
		perror("UI_SET_EVBIT(KEY)");
		return -1;
	}

	if (0 != ioctl(fdui, UI_SET_EVBIT, EV_SYN)) {
		perror("UI_SET_EVBIT(SYN)");
		return -1;
	}

	for (key = KEY_ESC; key <= KEY_MAX; key++) {
		res = ioctl(fdui, UI_SET_KEYBIT, key);
		if (0 > res) {
			fprintf(stderr, "SET_KEYBIT:%d:%m", errno);
			return -1;
		}
	}

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "kbdpipe");
	uidev.id.bustype = BUS_VIRTUAL;
	uidev.id.vendor  = 0xdead;
	uidev.id.product = 0xbeef;
	uidev.id.version = 1;
	res = write(fdui, &uidev, sizeof(uidev));
	if (0 > res) {
		fprintf(stderr, "uinput: %d:%m", res);
		return -1;
	}

	res = ioctl(fdui, UI_DEV_CREATE);
	if (0 > res) {
		perror("UI_DEV_CREATE");
		return -1;
	}

	fdpipe = open(pipename, O_RDONLY);
	if (0 > fdpipe) {
		fprintf(stderr, "%s:%d:%m", pipename, errno);
		return -1;
	}

	signal(SIGINT, ctrlc_handler);
	signal(SIGHUP, ctrlc_handler);

	while (!do_exit) {
		struct pollfd pfd;
		pfd.fd = fdpipe;
		pfd.events = POLLIN;
		if (1 == poll(&pfd, 1, 0xffffffff)) {
			char inbuf[256];
			int numread = read(fdpipe, inbuf, sizeof(inbuf)-1);
			if (numread) {
				unsigned long key;
				char *end;
				numread = trim(inbuf, numread);
				key = strtoul(inbuf, &end, 0);
				if (key && (key < KEY_MAX) &&
				    (end == (inbuf+numread))) {
					printf("key %ld/0x%lx\n", key, key);
					send_key(fdui, key);
				} else
					fprintf(stderr, "invalid input %s\n",
						inbuf);
			}
		}
	}

	res = unlink(pipename);
	if (0 > res) {
		fprintf(stderr, "rm:%s:%d:%m", pipename, errno);
		return -1;
	}

	return 0;
}
