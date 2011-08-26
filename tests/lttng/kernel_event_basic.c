/*
 * Copyright (c)  2011 David Goulet <david.goulet@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * as published by the Free Software Foundation; only version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <lttng/lttng.h>

#include "../utils.h"

int main(int argc, char **argv)
{
    struct lttng_handle *handle = NULL;
    struct lttng_domain dom;
	struct lttng_channel channel;
	struct lttng_event sched_switch;
	struct lttng_event sys_enter;
	struct lttng_event sys_exit;

    int ret = 0;

	char *session_name = "kernel_event";

    dom.type = LTTNG_DOMAIN_KERNEL;

	strcpy(channel.name, "mychan");
	channel.attr.overwrite = 0;
	channel.attr.subbuf_size = 4096;
	channel.attr.num_subbuf = 8;
	channel.attr.switch_timer_interval = 0;
	channel.attr.read_timer_interval = 200;
	channel.attr.output = LTTNG_EVENT_SPLICE;

	strcpy(sched_switch.name, "sched_switch");
	sched_switch.type = LTTNG_EVENT_TRACEPOINT;

	strcpy(sys_enter.name, "sys_enter");
	sys_enter.type = LTTNG_EVENT_TRACEPOINT;

	strcpy(sys_exit.name, "sys_exit");
	sys_exit.type = LTTNG_EVENT_TRACEPOINT;

	printf("\nTesting tracing kernel events:\n");
	printf("-----------\n");
	/* Check if root */
	if (getuid() != 0) {
		printf("Root access is needed.\nPlease run 'sudo make check' -- Aborting!\n");
		return 0;
	}

	if (argc < 2) {
		printf("Missing session trace path\n");
		return 1;
	}

	printf("Creating tracing session (%s): ", argv[1]);
    if ((ret = lttng_create_session(session_name, argv[1])) < 0) {
        printf("error creating the session : %s\n", lttng_get_readable_code(ret));
		goto create_fail;
    }
	PRINT_OK();

	printf("Creating session handle: ");
	if ((handle = lttng_create_handle(session_name, &dom)) == NULL) {
		printf("error creating handle: %s\n", lttng_get_readable_code(ret));
		goto handle_fail;
	}
	PRINT_OK();

	printf("Enabling %s kernel channel: ", channel.name);
	if ((ret = lttng_enable_channel(handle, &channel)) < 0) {
		printf("error enable channel: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}

	printf("Enabling %s kernel event: ", sched_switch.name);
    if ((ret = lttng_enable_event(handle, &sched_switch, channel.name)) < 0) {
        printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
    }
	PRINT_OK();

	printf("Enabling %s kernel event: ", sys_enter.name);
	if ((ret = lttng_enable_event(handle, &sys_enter, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Enabling %s kernel event: ", sys_exit.name);
	if ((ret = lttng_enable_event(handle, &sys_exit, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Disabling %s kernel event: ", sched_switch.name);
	if ((ret = lttng_disable_event(handle, sched_switch.name, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Disabling %s kernel event: ", sys_exit.name);
	if ((ret = lttng_disable_event(handle, sys_exit.name, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Renabling %s kernel event: ", sched_switch.name);
	if ((ret = lttng_enable_event(handle, &sched_switch, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Renabling %s kernel event: ", sys_exit.name);
	if ((ret = lttng_enable_event(handle, &sys_exit, channel.name)) < 0) {
		printf("error enabling event: %s\n", lttng_get_readable_code(ret));
		goto enable_fail;
	}
	PRINT_OK();

	printf("Start tracing: ");
	if ((ret = lttng_start_tracing(handle)) < 0) {
		printf("error starting tracing: %s\n", lttng_get_readable_code(ret));
		goto start_fail;
	}
	PRINT_OK();

	sleep(2);

	printf("Stop tracing: ");
	if ((ret = lttng_stop_tracing(handle)) < 0) {
		printf("error stopping tracing: %s\n", lttng_get_readable_code(ret));
		goto stop_fail;
	}
	PRINT_OK();

	printf("Destroy tracing session: ");
	if ((ret = lttng_destroy_session(handle)) < 0) {
		printf("error destroying session: %s\n", lttng_get_readable_code(ret));
	}
	PRINT_OK();

	return 0;

create_fail:
	assert(ret != 0);
handle_fail:
	assert(handle != NULL);

stop_fail:
start_fail:
enable_fail:
	lttng_destroy_session(handle);
	lttng_destroy_handle(handle);

    return 1;
}