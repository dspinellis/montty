/*
 * Unobtrusively log data coming in on a serial device.
 *
 * Diomidis Spinellis, December 2001 - July 2016
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <syslog.h>
#include <string.h>
#include <termios.h>

static int lockpid;

/*
 * Set terminal fd speed to s; clear non-blocking mode to make poll work
 */
static void
init_term(int fd, int s)
{
	struct termios tio;
	int val;

	if (tcgetattr(fd, &tio) < 0) {
		syslog(LOG_ERR, "unable get termios: %m");
		exit(1);
	}
	tio.c_iflag |= IGNBRK;
	tio.c_iflag &= ~(ICRNL | IMAXBEL | IXON | BRKINT);
	tio.c_oflag &= ~(OPOST | ONLCR);
	tio.c_cflag |= CS8;
	tio.c_cflag &= ~(CRTSCTS);
	tio.c_lflag |= NOFLSH;
	tio.c_lflag &= ~(ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE);
	if (cfsetspeed(&tio, s) < 0) {
		syslog(LOG_ERR, "unable to set speed to %d: %m", s);
		exit(1);
	}
	if (tcsetattr(fd, TCSAFLUSH, &tio) < 0) {
		syslog(LOG_ERR, "unable set termios: %m");
		exit(1);
	}
	if ((val = fcntl(fd, F_GETFL, 0)) < 0) {
		syslog(LOG_ERR, "fcntl(F_GETFL) failed: %m");
		exit(1);
	}
	val &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, val) < 0) {
		syslog(LOG_ERR, "fcntl(F_SETFL) failed: %m");
		exit(1);
	}
}

/*
 * Expand src to dst
 */
static void
expand(char *src, char *dst, int len)
{
	int n = 0;
	char *p;

	for (p = src, n = 0; *p && n < len - 1; p++, n++)
		if (*p == '\\')
			switch (*++p) {
			case '\\': *dst++ = '\\'; break;
			case 'a': *dst++ = '\a'; break;
			case 'b': *dst++ = '\b'; break;
			case 'f': *dst++ = '\f'; break;
			case 't': *dst++ = '\t'; break;
			case 'r': *dst++ = '\r'; break;
			case 'n': *dst++ = '\n'; break;
			case 'v': *dst++ = '\v'; break;
			case '0': *dst++ = '\0'; break;
			default:
				syslog(LOG_ERR, "invalid escape in %s", src);
				exit(1);
			}
		else
			*dst++ = *p;
	*dst = 0;
}

/* First argv used for initialisation */
#define INIT_ARGV 2

#ifndef UU_LOCK_OK

/* Return values from uu_lock(). */
#define UU_LOCK_INUSE           1
#define UU_LOCK_OK              0
#define UU_LOCK_OPEN_ERR        (-1)
#define UU_LOCK_READ_ERR        (-2)
#define UU_LOCK_CREAT_ERR       (-3)
#define UU_LOCK_WRITE_ERR       (-4)
#define UU_LOCK_LINK_ERR        (-5)
#define UU_LOCK_TRY_ERR         (-6)
#define UU_LOCK_OWNER_ERR       (-7)

static int
uu_lock(const char *ttyname)
{
	char try[1024];
	char final[1024];
	int fd;
	char buff[50];

	snprintf(try, sizeof(try), "/var/lock/LCK..%s.%d", ttyname,
			lockpid);
	snprintf(final, sizeof(final), "/var/lock/LCK..%s", ttyname);
	(void)unlink(try);
	if ((fd = open(try, O_CREAT | O_TRUNC | O_WRONLY, 0755)) == -1)
		return UU_LOCK_CREAT_ERR;
	sprintf(buff, "%d\n", lockpid);
	if (write(fd, buff, strlen(buff)) != strlen(buff)) {
			close(fd);
			return UU_LOCK_WRITE_ERR;
	}
	close(fd);
	if (rename(try, final) == -1) {
		(void)unlink(try);
		return UU_LOCK_INUSE;
	} else {
		(void)unlink(try);
		return UU_LOCK_OK;
	}
}

static int
uu_unlock(const char *ttyname)
{
	char lockname[1024];

	snprintf(lockname, sizeof(lockname), "/var/lock/LCK..%s", ttyname);
	return unlink(lockname);
}

static const char *
uu_lockerr(int e)
{
	switch (e) {
	case UU_LOCK_INUSE:
		return "Device is in use";
	case UU_LOCK_OPEN_ERR:
		return "File open error";
	case UU_LOCK_READ_ERR:
		return "File read error";
	case UU_LOCK_CREAT_ERR:
		return "File creation error";
	case UU_LOCK_WRITE_ERR:
		return "File write error";
	case UU_LOCK_LINK_ERR:
		return "File link error";
	case UU_LOCK_TRY_ERR:
		return "File try error";
	case UU_LOCK_OWNER_ERR:
		return "File owner error";
	}
}

#endif

main(int argc, char *argv[])
{
	FILE *f;
	char logname[1024];
	char buff[1024];
	char devname[1024];
	struct pollfd pfd[1];
	int n;
	/* True when initialisation strings must be sent */
	int need_init = 1;
	int init_index = INIT_ARGV;
	int lockresult;

	if (argc < 2) {
		fprintf(stderr, "usage: %s line [initialisation string] ...\n"
				"e.g. %s ttyACM0 'ATS82=76\\r\\n'\n", argv[0], argv[0]);
		exit(1);
	}
	daemon(0, 0);
	lockpid = getpid();
	snprintf(logname, sizeof(logname), "montty.%s", argv[1]);
	openlog(logname, 0, LOG_LOCAL0);
	syslog(LOG_INFO, "starting up: pid %d", getpid());
	snprintf(buff, sizeof(buff), "/var/run/montty.%s.pid", argv[1]);
	if ((f = fopen(buff, "w")) == NULL) {
		syslog(LOG_ERR, "unable to open pid file %s: %m", buff);
		exit(1);
	} else {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	snprintf(devname, sizeof(devname), "/dev/%s", argv[1]);
	if ((pfd[0].fd = open(devname, O_RDWR | O_NONBLOCK)) < 0) {
		syslog(LOG_ERR, "unable to open monitor file %s: %m", devname);
		exit(1);
	}
	syslog(LOG_INFO, "monitoring %s", devname);
	pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
	for (;;) {
		if (!need_init) {
			/* No initialisation needed, just wait for input */
			syslog(LOG_DEBUG, "waiting for input");
			if (poll(pfd, 1, -1) < 0) {
				syslog(LOG_ERR, "poll(INFTIM) failed: %m");
				exit(1);
			}
		}
		/*
		 * We have input, or we need to initialise the device;
		 * acquire a lock.
		 */
		switch (lockresult = uu_lock(argv[1])) {
		case UU_LOCK_OK:
			syslog(LOG_DEBUG, "acquired lock");
			/*
			 * Now that we have the lock,
			 * check if we need to write the initialisation data.
			 */
			if (need_init) {
				if (init_index == INIT_ARGV) {
					syslog(LOG_DEBUG, "recycle fd");
					/* Refresh fd */
					close(pfd[0].fd);
					if ((pfd[0].fd = open(devname, O_RDWR | O_NONBLOCK)) < 0) {
						syslog(LOG_ERR, "unable to re-open monitor file %s: %m", devname);
						exit(1);
					}
					init_term(pfd[0].fd, B115200);
				}
				if (init_index < argc) {
					expand(argv[init_index], buff, sizeof(buff));
					syslog(LOG_DEBUG, "write: %s", buff);
					if (write(pfd[0].fd, buff, strlen(buff)) != strlen(buff)) {
						syslog(LOG_ERR, "write failed: %m");
						exit(1);
					}
					init_index++;
					sleep(1);
				} 
				if (init_index == argc)
					need_init = 0;
				syslog(LOG_DEBUG, "sent init string");
			}
			/* Check if there is still something to read. */
			if (poll(pfd, 1, 0) < 0) {
				syslog(LOG_ERR, "poll(0) failed: %m");
				exit(1);
			}
			if (pfd[0].revents & (POLLIN | POLLRDNORM)) {
				if ((n = read(pfd[0].fd, buff, sizeof(buff))) == -1) {
					syslog(LOG_ERR, "read failed: %m");
					exit(1);
				}
				syslog(LOG_DEBUG, "read %d bytes", n);
				buff[n] = 0;
				syslog(LOG_INFO, "%s", buff);
			}
			if (uu_unlock(argv[1]) == -1) {
				syslog(LOG_ERR, "uu_unlock error %m");
				exit(1);
			}
			syslog(LOG_DEBUG, "lock released");
			break;
		case UU_LOCK_INUSE:
			syslog(LOG_DEBUG, "lock in use; sleeping");
			sleep(1);
			/* Someone is using the device, we must re-init it */
			need_init = 1;
			init_index = INIT_ARGV;
			break;
		default:
			syslog(LOG_ERR, "uu_lock error %s", uu_lockerr(lockresult));
		}
	}
}
