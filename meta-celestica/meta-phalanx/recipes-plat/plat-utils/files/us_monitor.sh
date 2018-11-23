#!/bin/bash
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

. /usr/local/bin/openbmc-utils.sh

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/bin

PSU_NUM=2
psu_status=(0 0)
psu_path=(
"/sys/bus/i2c/devices/i2c-25/25-0059/hwmon"
"/sys/bus/i2c/devices/i2c-24/24-0058/hwmon"
)
psu_register=(
"i2c_device_delete 25 0x59;i2c_device_delete 25 0x51;i2c_device_add 25 0x59 dps1100;i2c_device_add 25 0x51 24c32"
"i2c_device_delete 24 0x58;i2c_device_delete 24 0x50;i2c_device_add 24 0x58 dps1100;i2c_device_add 24 0x50 24c32"
)

psu_status_init() {
	id=0
	for i in "${psu_path[@]}"
	do
		if [ -e "$i" ]; then
			psu_status[$id]=1
		else
			logger "Warning: PSU $(($id + 1)) is absent"
			psu_status[$id]=0
		fi
		id=$(($id + 1))
	done
}

read_info() {
    echo `cat /sys/bus/i2c/devices/i2c-${1}/${1}-00${2}/${3} | head -n 1`
}

psu_present() {
    if [ $1 -eq 1  ]; then
        ((val=$(read_info 0 0d psu_r_present)))
    else
        ((val=$(read_info 0 0d psu_l_present)))
    fi
    if [ $val -eq 0 ]; then
        return 1
    else
        return 0
    fi
}

psu_power() {
    if [ $1 -eq 1  ]; then
        ((val=$(read_info 0 0d psu_r_status)))
    else
        ((val=$(read_info 0 0d psu_l_status)))
    fi
    if [ $val -eq 0 ]; then
        return 1
    else
        return 0
    fi
}


psu_status_check() {
	if [ $# -le 0 ]; then
		return 1
	fi

	psu_present $(($1 + 1))
	if [ $? -eq 1 ]; then
		psu_power $(($1 + 1))
		power_ok=$?
		if [ ${psu_status[$1]} -eq 0 ] && [ $power_ok -eq 0 ]; then
			logger "us_monitor: Register PSU $(($i + 1))"
			eval "${psu_register[$1]}"
			psu_status_init
			return 0
		fi
	fi
}

psu_status_init
come_rest_status 1
come_rst_st=$?
while true; do
	for((i = 0; i < $PSU_NUM; i++))
	do
		psu_status_check $i
	done

	come_rest_status
	if [ $? -ne $come_rst_st ]; then
		come_rest_status 2
		come_rst_st=$?
	fi

    usleep 500000
done
