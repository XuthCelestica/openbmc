#!/usr/bin/env python3
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


from subprocess import Popen, PIPE
import re


# Read all contents of file path specified.
def read_file_contents(path):
    try:
        with open(path, 'r') as proc_file:
            content = proc_file.readlines()
    except IOError as e:
        content = None

    return content


# Handler for FRUID resource endpoint
def get_bmc():
    # Get BMC Reset Reason
    (wdt_counter, _) = Popen('devmem 0x1e785010',
                             shell=True, stdout=PIPE).communicate()
    wdt_counter = int(wdt_counter, 0)

    wdt_counter &= 0xff00

    if wdt_counter:
        por_flag = 0
    else:
        por_flag = 1

    if por_flag:
        reset_reason = "Power ON Reset"
    else:
        reset_reason = "User Initiated Reset or WDT Reset"

    # Get BMC's Up Time
    uptime = Popen('uptime', shell=True, stdout=PIPE).stdout.read().decode().strip('\n')

    # Use another method, ala /proc, but keep the old one for backwards
    # compat.
    # See http://man7.org/linux/man-pages/man5/proc.5.html for details
    # on full contents of proc endpoints.
    uptime_seconds = read_file_contents("/proc/uptime")[0].split()[0]

    # Pull load average directory from proc instead of processing it from
    # the contents of uptime command output later.
    load_avg = read_file_contents("/proc/loadavg")[0].split()[0:3]

    # Get Usage information
    (data, _) = Popen('top -b n1',
                      shell=True, stdout=PIPE).communicate()
    data = data.decode()
    adata = data.split('\n')
    mem_usage = adata[0]
    cpu_usage = adata[1]

    # Get disk usage information
    (data, _) = Popen('df | grep emmc',
                       shell=True, stdout=PIPE).communicate()
    data = data.decode()
    adata = data.split()
    disk_usage = 'EMMC: {}K total, {}K used, {}K available, {} use'.format(adata[1], adata[2], adata[3], adata[4])

    # Get OpenBMC version
    version = ""
    (data, _) = Popen('cat /etc/issue',
                      shell=True, stdout=PIPE).communicate()
    data = data.decode()
    ver = re.search(r'v([\w\d._-]*)\s', data)
    if ver:
        version = ver.group(1)
    
    # Get U-Boot version
    uboot_version = ""
    (data, _) = Popen('strings /dev/mtd0 | grep U-Boot | grep 2016',
                      shell=True, stdout=PIPE).communicate()
    data = data.decode()
    ver = re.search(r'v([\w\d._-]*)\s', data)
    if ver:
        uboot_version = ver.group(1)

    # Get CPLD version
    syscpld_version = ""
    (data, _) = Popen('cat /sys/bus/i2c/devices/0-000d/version | head -n 1',
                       shell=True, stdout=PIPE).communicate()
    data = data.decode()
    if len(data) > 2:
        syscpld_version = '{:0>2}'.format(data[2:].strip('\n'))
    else:
        syscpld_version = 'null'
    
    fancpld_version = ""
    (data, _) = Popen('cat /sys/bus/i2c/devices/8-000d/version | head -n 1',
                       shell=True, stdout=PIPE).communicate()
    data = data.decode()
    if len(data) > 2:
        fancpld_version = '{:0>2}'.format(data[2:].strip('\n'))
    else:
        fancpld_version = 'null'

    result = {
                "Information": {
                    "Description": "Fishbone BMC",
                    "Reset Reason": reset_reason,
                    # Upper case Uptime is for legacy
                    # API support
                    "Uptime": uptime,
                    # Lower case Uptime is for simpler
                    # more pass-through proxy
                    "uptime": uptime_seconds,
                    "load-1": load_avg[0],
                    "load-5": load_avg[1],
                    "load-15": load_avg[2],
                    "Disk Usage":disk_usage,
                    "Memory Usage": mem_usage,
                    "CPU Usage": cpu_usage,
                    "OpenBMC Version": version,
                    "U-Boot Version": uboot_version,
                    "SysCPLD Version":syscpld_version,
                    "FanCPLD Version":fancpld_version,
                },
                "Actions": [],
                "Resources": [],
             }

    return result
