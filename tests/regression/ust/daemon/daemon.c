/*
 * Copyright (C) 2009  Pierre-Marc Fournier
 * Copyright (C) 2011-2012  Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; version 2.1 of
 * the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>

#define TRACEPOINT_DEFINE
#define TRACEPOINT_CREATE_PROBES
#include "ust_tests_daemon.h"

int main(int argc, char **argv, char *env[])
{
	int result;

	if (argc < 2) {
		fprintf(stderr, "usage: daemon pid_file\n");
		exit(1);
	}

	pid_t parent_pid = getpid();
	tracepoint(ust_tests_daemon, before_daemon, parent_pid);

	result = daemon(0, 1);
	if (result == 0) {
		int pid_fd;
		uint8_t size_pid = (uint8_t) sizeof(pid_t);
		pid_t child_pid = getpid();

		tracepoint(ust_tests_daemon, after_daemon_child, child_pid);

		if (mkfifo(argv[1], 666) < 0) {
			perror("mkfifo");
			result = 1;
			goto end;
		}

		pid_fd = open(argv[1], O_WRONLY);
		if (pid_fd < 0) {
			perror("open");
		}
		write(pid_fd, &size_pid, sizeof(size_pid));
		write(pid_fd, &parent_pid, sizeof(parent_pid));
		write(pid_fd, &child_pid, sizeof(child_pid));

		if (close(pid_fd) < 0) {
			perror("close");
			result = 1;
			goto end;
		}
	} else {
		tracepoint(ust_tests_daemon, after_daemon_parent);
		perror("daemon");
		result = 1;
		goto end;
	}
	result = 0;
end:
	return result;
}
