#!/usr/bin/env python
#
# Copyright 2014-present Facebook. All Rights Reserved.
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
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

import json
import re
from subprocess import *
import bmc_command

# Handler for fan resource endpoint
def get_fan_ctrl():
    result = []
    data = Popen('ps |grep fand |grep -v grep| awk -F \" \" \'{print $1}\'',
                  \
 	          shell=True,
                  stdout=PIPE).stdout.read()
    adata = data.split('\n')
    if adata[0]:
        fresult = {
                "Information": "enable",
                "Actions": ["enable", "disable"],
                "Resources": [],
              }

    else:
        fresult = {
                "Information": "disable",
                "Actions": ["enable", "disable"],
                "Resources": [],
              }
    return fresult

def fan_ctrl_action(data):
    length = len(data)
    value = data["action"]
    status = Popen('ps |grep fand |grep -v grep| awk -F \" \" \'{print $1}\'',
                  \
                  shell=True,
                  stdout=PIPE).stdout.read()
    adata = status.split('\n')

    if value == 'enable':
        if adata[0]:
            fresult = {
                "Information": "enable",
                "Actions": [],
                "Resources": [],
              }
            return fresult
        else:
            cmd = '/usr/local/bin/fand'
    elif value == 'disable':
        if adata[0]:
            cmd = 'kill ' + adata[0]
        else:
            fresult = {
                "Information": "disable",
                "Actions": [],
                "Resources": [],
              }
            return result
    else:
        result = { "result": "error"}
        return result

    ret = Popen(cmd, shell=True, stdout=PIPE).stdout.read()
    result = { "result": "success"}
    return result
