/*
 * Unobtrusively log data coming in on a serial device.
 *
 */

/*
 * XXX Send ATS82=76\r\n
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <syslog.h>

main(int argc, char *argv[])
{
	FILE *f;
	char logname[1024];
	char buff[1024];
	char devname[1024];
	char lockname[1024];
	struct pollfd pfd[1];
	int n;

	if (argc != 2) {
		fprintf(stderr, "%s: no line specified\n", argv[0]);
		exit(1);
	}
	daemon(0, 0);
	snprintf(logname, sizeof(logname), "montty.%s", argv[1]);
	openlog(logname, 0, LOG_LOCAL0);
	syslog(LOG_INFO, "starting up: pid %d logging on %s", getpid(), logname);
	snprintf(buff, sizeof(buff), "/var/run/montty.%s.pid", argv[1]);
	if ((f = fopen(buff, "w")) == NULL) {
		syslog(LOG_ERR, "unable to open pid file %s: %m", buff);
		exit(1);
	} else {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	snprintf(devname, sizeof(devname), "/dev/%s", argv[1]);
	if ((pfd[0].fd = open(devname, O_RDONLY | O_NONBLOCK)) < 0) {
		syslog(LOG_ERR, "unable to open monitor file %s: %m", devname);
		exit(1);
	}
	snprintf(lockname, sizeof(lockname), "/var/spool/lock/LCK..%s", argv[1]);
	syslog(LOG_INFO, "monitoring %s", devname);
	syslog(LOG_INFO, "lock file is %s", lockname);
	pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
	/*
	 * Various race conditions here...
	 */
	for (;;) {
		if (poll(pfd, 1, INFTIM) < 0) {
			syslog(LOG_ERR, "poll failed: %m");
			exit(1);
		}
		if (access(lockname, F_OK) == 0)
			/* Data is not for us */
			sleep(1);
		else {
			if ((n = read(pfd[0].fd, buff, sizeof(buff))) == -1) {
				syslog(LOG_ERR, "read failed: %m");
				exit(1);
			}
			buff[n] = 0;
			syslog(LOG_INFO, "%s", buff);
		}
	}
}
