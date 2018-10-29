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

# Handler for psu1 resource endpoint
def get_psu():
    result = []
    data = Popen('psu_info',
                  \
 	          shell=True,
                  stdout=PIPE).stdout.read()
    adata = data.split('\n')
    fresult = {
                "Information": adata,
                "Actions": ["on", "off"],
                "Resources": [],
              }
    return fresult

def psu_action(data):
    length = len(data)
    opt = data["action"]
    ppsu = data["psu"]
    cmd = 'sensors_setup.sh stop'
    ret = Popen(cmd, shell=True, stdout=PIPE).stdout.read()
    if ppsu == 'psu1':
        channel  = '0x40'
        addr = '0x59'
        cmd1 = 'i2cset -f -y 7 0x70 0x0 ' + channel
    elif ppsu == 'psu2':
        channel  = '0x80'
        addr = '0x5a'
        cmd1 = 'i2cset -f -y 7 0x70 0x0 ' + channel
    else:
        result = { "result": "error"}
        return result
    if opt == 'on':
        cmd2 = 'i2cset -f -y 7 ' + addr + ' 0x1 0x80'
    elif opt == 'off':
        cmd2 = 'i2cset -f -y 7 ' + addr + ' 0x1 0x0'
    else:
        result = { "result": "error"} 
        return result
    ret1 = Popen(cmd1, shell=True, stdout=PIPE).stdout.read()
    ret2 = Popen(cmd2, shell=True, stdout=PIPE).stdout.read()
    result = { "result": "success"}
    return result   
