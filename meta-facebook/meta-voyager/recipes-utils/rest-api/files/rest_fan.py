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
def get_fan():
    result = []
    data = Popen('/usr/local/bin/get_fan_speed.sh',
                  \
 	          shell=True,
                  stdout=PIPE).stdout.read()
    adata = data.split('\n')
    fresult = {
                "Information": adata,
                "Actions": ["percent:0-100","num:1-5"],
                "Resources": [],
              }
    return fresult

def fan_action(data):
    length = len(data)
    value = data["action"]
    if int(value, 10) >= 0 and int(value, 10) <= 100:
        if length > 1:
            fnum = data["num"]
            if int(fnum, 10) >= 1 and int(fnum, 10) <= 5:
                cmd = '/usr/local/bin/set_fan_speed.sh ' + str(value) + ' ' + str(fnum)
            else:
                result = { "result": "error"}
                return result
        else:
            cmd = '/usr/local/bin/set_fan_speed.sh ' + str(value)
    else:
        result = { "result": "error"}
        return result
    ret = Popen(cmd, shell=True, stdout=PIPE).stdout.read()
    result = { "result": "success"}
    return result    



