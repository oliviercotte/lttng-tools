#!/usr/bin/env python3
#
# Copyright (C) - 2013 Jérémie Galarneau <jeremie.galarneau@efficios.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License, version 2 only, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import uuid
import os
import subprocess
import re
import shutil
import sys
import tempfile
import struct

test_path = os.path.dirname(os.path.abspath(__file__)) + "/"
test_utils_path = test_path
for i in range(4):
    test_utils_path = os.path.dirname(test_utils_path)
test_utils_path = test_utils_path + "/utils"
sys.path.append(test_utils_path)
from test_utils import *


NR_TESTS = 6
current_test = 1
print("1..{0}".format(NR_TESTS))

# Check if a sessiond is running... bail out if none found.
if session_daemon_alive() == 0:
    bail("No sessiond running. Please make sure you are running this test with the \"run\" shell script and verify that the lttng tools are properly installed.")

tmp_directory = tempfile.mkdtemp()
daemon_pid_pipe_path = tmp_directory + "/daemon.pipe"

session_info = create_session()
enable_ust_tracepoint_event(session_info, "*")
start_session(session_info)

print("Starting daemon process...")
daemon_process = subprocess.Popen([test_path + "daemon", daemon_pid_pipe_path])

if sys.version_info >= (3, 3):
    try:
        daemon_process.wait(5)
    except TimeoutExpired:
        daemon_process.kill()
        bail("Failed to run daemon test application (time out)", session_info)
else:
    daemon_process.wait()

print("Came back from daemon process...")
parent_pid = -1
child_pid = -1

print_test_result(daemon_process.returncode == 0, current_test, "Successful call to daemon() and normal exit")
current_test += 1

if daemon_process.returncode != 0:
    bail("Could not trigger tracepoints successfully. Abondoning test.")

stop_session(session_info)

print("Open pipe...")
try:
    pid_pipe = open(daemon_pid_pipe_path, 'r')
except (IOError, OSError):
    bail("Unable to open pid pipe.")

print("Read of size 1...")
pid_type_size = struct.unpack("=B", pid_pipe.read(1))
print("pid_t size is : {}", pid_type_size)

parent_pid = struct.unpack("=i", pid_pipe.read(pid_type_size))
print("parent_pid = {}", parent_pid)
child_pid = struct.unpack("=i", pid_pipe.read(pid_type_size))
print("child_pid = {}", child_pid)

if parent_pid == -1 or child_pid == -1:
    bail("Unexpected output received from daemon test executable." + str(daemon_process_output))

try:
    babeltrace_process = subprocess.Popen(["babeltrace", session_info.trace_path], stdout=subprocess.PIPE)
except FileNotFoundError:
    bail("Could not open babeltrace. Please make sure it is installed.")

before_daemon_event_found = False
before_daemon_event_pid = -1
after_daemon_event_found = False
after_daemon_event_pid = -1

for event_line in babeltrace_process.stdout:
    event_line = event_line.decode('utf-8').replace("\n", "")

    if re.search(r"before_daemon", event_line) is not None:
        if before_daemon_event_found:
            bail("Multiple instances of the before_daemon event found. Please make sure only one instance of this test is runnning.")
        before_daemon_event_found = True
        match = re.search(r"(?<=pid = )\d+", event_line)

        if match is not None:
            before_daemon_event_pid = match.group(0)

    if re.search(r"after_daemon", event_line) is not None:
        if after_daemon_event_found:
            bail("Multiple instances of the after_daemon event found. Please make sure only one instance of this test is runnning.")
        after_daemon_event_found = True
        match = re.search(r"(?<=pid = )\d+", event_line)

        if match is not None:
            after_daemon_event_pid = match.group(0)
babeltrace_process.wait()

print_test_result(babeltrace_process.returncode == 0, current_test, "Resulting trace is readable")
current_test += 1

if babeltrace_process.returncode != 0:
    bail("Unreadable trace; can't proceed with analysis.")

print_test_result(before_daemon_event_found, current_test, "before_daemon event found in resulting trace")
current_test += 1
print_test_result(before_daemon_event_pid == parent_pid, current_test, "Parent pid reported in trace is correct")
current_test += 1
print_test_result(before_daemon_event_found, current_test, "after_daemon event found in resulting trace")
current_test += 1
print_test_result(after_daemon_event_pid == daemon_pid, current_test, "Daemon pid reported in trace is correct")
current_test += 1

shutil.rmtree(session_info.tmp_directory)
