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

FANS=4
FAN_DIR=/sys/bus/i2c/devices/i2c-8/8-000d/

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

get_fan_pwm() {
    for i in $FANS ; do
        pwm_node="${FAN_DIR}/fan${i}_pwm"
        val=$(cat $pwm_node | head -n 1)
        if [ $((val * 100 % 255)) -ne 0 ]; then
            pwm=$((val * 100 / 255 + 1))
        else
            pwm=$((val * 100 / 255))
        fi
        if [ $pwm -gt 0 ]; then
            return $pwm
        fi
    done

    return 0
}

get_board_type() {
    brd_type=$(cat /sys/bus/i2c/devices/0-000d/sw_brd_type | head -n 1)
    if [ $brd_type == 0x1 ]; then
        echo "fishbone48"
    elif [ $brd_type == 0x0 ]; then
        echo "fishbone32"
    else
        echo ""
    fi
}

get_fan_dir() {
    for i in $FANS ; do
        val=$(/usr/local/bin/fruid-util fan$i |grep R1241-F9001)
        if [ -n "$val" ]; then
            echo "F2B"
            return 1
        fi
    done
    echo "B2F"
}

inlet_sensor_revise() {
    get_fan_pwm
    pwm=$?
    direction=$(get_fan_dir)
    if [ "$direction" = "F2B" ]; then
        board=$(get_board_type)
        if [ "$board" = "fishbone48" ]; then
            if [ $pwm -le 45 ]; then
                temp=7
            elif [ $pwm -ge 70 ]; then
                temp=3
            else
                temp=5
            fi
            if [ $temp -ne $1 ]; then
                echo 70000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max
                echo 60000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max_hyst
                cmd="sed -i '/compute temp1/c compute temp1 @-($temp), @/($temp)' /etc/sensors.d/fishbone.conf"
                eval $cmd
            fi
        elif [ "$board" = "fishbone32" ]; then
            if [ $pwm -le 50 ]; then
                temp=6
            elif [ $pwm -ge 81 ]; then
                temp=1
            else
                temp=3
            fi
            if [ $temp -ne $1 ]; then
                echo 70000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max
                echo 60000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max_hyst
                cmd="sed -i '/compute temp1/c compute temp1 @-($temp), @/($temp)' /etc/sensors.d/fishbone.conf"
                eval $cmd
            fi
        fi
    fi
    return $temp
}

cpu_temp_update() {
    temp=$(get_cpu_temp)
    val=$(($temp*1000))
    echo $val >/sys/bus/i2c/devices/i2c-0/0-000d/temp2_input
}

psu_status_init
come_rest_status 1
come_rst_st=$?
revise_temp=0
cpu_update=0
echo 70000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max
echo 60000 >/sys/bus/i2c/devices/i2c-7/7-004d/hwmon/hwmon3/temp1_max_hyst
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
    inlet_sensor_revise $revise_temp
    revise_temp=$?

    cpu_update=$((cpu_update+1))
    if [ $cpu_update -ge 6 ]; then
        cpu_temp_update
        cpu_update=0
    fi

    usleep 500000
done
