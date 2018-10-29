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
import subprocess
import bmc_command
from subprocess import *

# Handler for led1 resource endpoint
def get_led1():
    data = Popen(['cat /sys/bus/i2c/devices/i2c-13/13-0031/led_ctrl'], \
                        shell=True, stdout=PIPE).stdout.read()
    adata = data.split('\n')
    value = adata[0]
    temp = int(value, 16)
    if temp & 0x1:
        result = 'Red '
    elif temp & 0x2:
        result = 'Green '
    elif temp & 0x4:
        result = 'Blue'
    elif temp & 0x7 == 0:
        result = 'off'
    else:
        result = 'unknow '
	
    if temp & 0x8:
        blink = 'yes'
    else:
        blink = 'no'

    fresult = {
			    "Information": {"LED": result,
					"blink": blink},
				"Actions": ["Red, blink", "Green, blink", "Blue, blink", "off"],
                "Resources": [],
              }
    return fresult

def led1_action(data):
    length = len(data)
    value = data["action"]
    blink = data["blink"]
    if value == 'Red' or value == 'red':
        adata = 0x1
    elif value == 'Green' or value == 'green':
        adata = 0x2
    elif value == 'Blue' or value == 'blue':
        adata = 0x4
    elif value == 'Off' or value == 'off':
        adata = 0

    if length > 1:
        blink = data["blink"]
        if blink == 'Yes' or blink == 'yes':
            adata |= 0x8

    cmd = 'echo ' + str(adata) + ' >/sys/bus/i2c/devices/i2c-13/13-0031/led_ctrl'
    ret = Popen(cmd, shell=True, stdout=PIPE).stdout.read()
    if ret == '':
        result = { "result": "success"}
    else:
        result = { "result": "fail" }

    return result
