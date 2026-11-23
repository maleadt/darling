// Copyright (C) 2026 Tim Besard
//
// syslogd drains its BSD socket, /var/run/syslog, after startup: a burst of
// datagrams far larger than the socket's receive queue goes through.
//
// A blocking send waits while the queue is full, so a syslogd that does not
// read the socket stalls every process that logs through it (syslog(3), and
// libdispatch or libmalloc reporting a problem). An alarm bounds the wait. Run
// it right after restarting Darling on an existing prefix (darling shutdown,
// then darling shell): the first boot of a new prefix does not show the problem.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#define BURST 1000
#define TIMEOUT 60

static volatile sig_atomic_t sent;

static void on_alarm(int sig)
{
	char buf[128];
	int n = snprintf(buf, sizeof(buf), "FAIL: sending to %s blocked after %d of %d datagrams\n",
		_PATH_LOG, (int)sent, BURST);
	write(STDOUT_FILENO, buf, n);
	_exit(1);
}

int main(void)
{
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	strlcpy(sun.sun_path, _PATH_LOG, sizeof(sun.sun_path));
	if (fd < 0 || connect(fd, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
		perror("FAIL: connect " _PATH_LOG);
		return 1;
	}

	signal(SIGALRM, on_alarm);
	alarm(TIMEOUT);
	char dgram[64];
	for (int i = 0; i < BURST; i++) {
		int n = snprintf(dgram, sizeof(dgram), "<13>syslogd_bsd_socket[%d]: %d", getpid(), i);
		if (send(fd, dgram, n, 0) < 0) {
			perror("FAIL: send");
			return 1;
		}
		sent = i + 1;
	}
	alarm(0);
	close(fd);

	printf("PASS: %d datagrams sent to %s\n", BURST, _PATH_LOG);
	return 0;
}
