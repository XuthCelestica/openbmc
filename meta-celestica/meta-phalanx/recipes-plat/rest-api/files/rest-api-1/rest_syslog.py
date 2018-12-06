#!/usr/bin/env python3
#
# Copyright 2018-present Celestica. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#

from subprocess import Popen, PIPE
import json
import re
import subprocess
import bmc_command

def syslog_action(data):
    result = []
    addr = data["addr"]

    (data, _) = Popen('wc -l /etc/rsyslog.conf',
                       shell=True, stdout=PIPE).communicate()
    conf_line = data.decode().split()
    conf_line = int(conf_line[0], 10) - 1
    if conf_line < 0:
        conf_line = 0
    proc = subprocess.Popen("head /etc/rsyslog.conf -n {} > /etc/rsyslog.conf_;"
                            "echo '*.*' @{}:1514 >> /etc/rsyslog.conf_;"
                            "mv /etc/rsyslog.conf_ /etc/rsyslog.conf;"
                            "/etc/init.d/syslog.rsyslog restart".format(conf_line, addr),
                            shell=True, stdout=PIPE, stderr=PIPE)
    try:
        data, err = bmc_command.timed_communicate(proc)
        data = data.decode().split('\n')
    except bmc_command.TimeoutError as ex:
        data = ex.output
        err = ex.error

    if len(data) >= 2:
        if data[0].strip() == 'stopping rsyslogd ... done' and \
           data[1].strip() == 'starting rsyslogd ... done':
            result = {"result": "success"}
        else:
            result = {"result": "fail"}
    else:
        result = {"result": "fail"}

    return result

