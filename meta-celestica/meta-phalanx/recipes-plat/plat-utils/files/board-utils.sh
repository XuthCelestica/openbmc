#!/bin/bash

SYSCPLD_SYSFS_DIR="/sys/bus/i2c/devices/i2c-0/0-000d"
HARDWARE_VERSION="${SYSCPLD_SYSFS_DIR}/hardware_version"
USRV_STATUS_SYSFS="${SYSCPLD_SYSFS_DIR}/come_status"
PWR_BTN_SYSFS="${SYSCPLD_SYSFS_DIR}/cb_pwr_btn_n"
PWR_RESET_SYSFS="${SYSCPLD_SYSFS_DIR}/come_rst_n"
SYSLED_CTRL_SYSFS="${SYSCPLD_SYSFS_DIR}/sysled_ctrl"
SYSLED_SEL_SYSFS="${SYSCPLD_SYSFS_DIR}/sysled_select"
SYSLED_SOL_CTRL_SYSFS="${SYSCPLD_SYSFS_DIR}/sol_control"
SYSLED_BCM5387_RST_SYSFS="${SYSCPLD_SYSFS_DIR}/bcm5387_reset"
BIOS_CHIPSELECTION="${SYSCPLD_SYSFS_DIR}/bios_cs"
BIOS_CTRL="${SYSCPLD_SYSFS_DIR}/bios_ctrl"
BIOS_BOOT_STATUS="${SYSCPLD_SYSFS_DIR}/bios_boot_ok"
BIOS_BOOT_CHIP="${SYSCPLD_SYSFS_DIR}/bios_boot_cs"
COME_RESET_STATUS_SYSFS="${SYSCPLD_SYSFS_DIR}/come_rst_st"

wedge_power() {
	if [ "$1" == "on" ]; then
		echo 0 > $PWR_BTN_SYSFS
		sleep 1
		echo 1 > $PWR_BTN_SYSFS
	elif [ "$1" == "off" ]; then
		echo 0 > $PWR_BTN_SYSFS
		sleep 5
		echo 1 > $PWR_BTN_SYSFS
	elif [ "$1" == "reset" ]; then
		echo 0 > $PWR_RESET_SYSFS
		sleep 10
	else
		echo -n "Invalid parameter"
		return 1
	fi
	wedge_is_us_on
	return $?
}

wedge_is_us_on() {
    local val n retries prog
    if [ $# -gt 0 ]; then
        retries="$1"
    else
        retries=1
    fi
    if [ $# -gt 1 ]; then
        prog="$2"
    else
        prog=""
    fi
    if [ $# -gt 2 ]; then
        default=$3              # value 0 means defaul is 'ON'
    else
        default=1
    fi
    ((val=$(cat $USRV_STATUS_SYSFS 2> /dev/null | head -n 1)))
	if [ -z "$val" ]; then
        return $default
	elif [ $val -eq 15 ]; then
        return 0            # powered on
    elif [ $val -eq 8 ]; then
        return 1
	else
		echo -n "read value $val "
		return 2
    fi
}

sys_led_usage() {
	echo "option: "
	echo "<green| yellow| mix| off> #LED color select"
	echo "<on| off| fast| slow>     #LED turn on, off or blink"
	echo 
}

sys_led_show() {
	local val
	sel=$(cat $SYSLED_SEL_SYSFS 2> /dev/null | head -n 1)
	ctrl=$(cat $SYSLED_CTRL_SYSFS 2> /dev/null | head -n 1)

	case "$sel" in
		0x0)
			val="green and yellow"
			;;
		0x1)
			val="green"
			;;
		0x2)
			val="yellow"
			;;
		0x3)
			val="off"
			;;
	esac
	echo -n "LED: $val "
	
	case "$ctrl" in
		0x0)
			val="on"
			;;
		0x1)
			val="1HZ blink"
			;;
		0x2)
			val="4HZ blink"
			;;
		0x3)
			val="off"
			;;
	esac
	echo "$val"
}

sys_led() {
	if [ $# -lt 2 ]; then
		sys_led_show
		return 0
	fi
	if [ "$1" == "green" ]; then
		echo 0x1 > $SYSLED_SEL_SYSFS
	elif [ "$1" == "yellow" ]; then
		echo 0x2 > $SYSLED_SEL_SYSFS
	elif [ "$1" == "mix" ]; then
		if [ "$2" == "on" ];then
			echo "Mode [mix on] not support!"
			return 1
		fi
		echo 0x0 > $SYSLED_SEL_SYSFS
	elif [ "$1" == "off" ]; then
		echo 0x3 > $SYSLED_SEL_SYSFS
	else
		sys_led_usage
		return 1
	fi

	if [ "$2" == "on" ]; then
		echo 0x0 > $SYSLED_CTRL_SYSFS
	elif [ "$2" == "fast" ]; then
		echo 0x2 > $SYSLED_CTRL_SYSFS
	elif [ "$2" == "slow" ]; then
		echo 0x1 > $SYSLED_CTRL_SYSFS
	elif [ "$2" == "off" ]; then
		echo 0x3 > $SYSLED_CTRL_SYSFS
	else
		sys_led_usage
		return 1
	fi
}

board_type() {
	((val=$(i2cget -f -y 0 0x0d 0x02 2> /dev/null | head -n 1)))
	if [ $val -eq '3' ]; then
		echo 'Phalanx'
	fi
}

sol_ctrl() {
	if [ "$1" = "BMC" ]; then
		echo 0 > $SYSLED_SOL_CTRL_SYSFS
	elif [ "$1" = "COME" ]; then
		echo 1 > $SYSLED_SOL_CTRL_SYSFS
	fi
}

boot_from() {
	if [ $# -lt 1 ]; then
		echo ""
		echo "Please indicate master or slave"
		return 1
	fi

	if [ "$1" = "master" ]; then
		devmem 0x1e785024 32 0x00989680
		devmem 0x1e785028 32 0x4755
		devmem 0x1e78502c 32 0x00000013
	elif [ "$1" = "slave" ]; then
		devmem 0x1e785024 32 0x00989680
		devmem 0x1e785028 32 0x4755
		devmem 0x1e78502c 32 0x00000093
	else
		echo "Error parameter!"
		return 1
	fi
}

bios_upgrade() {
        source /usr/local/bin/openbmc-utils.sh
        gpio_set E4 1

        if [ ! -c /dev/spidev1.0 ]; then
                mknod /dev/spidev1.0 c 153 0
        fi
        modprobe spidev

        if [ "$1" == "master" ]; then
                echo 0x2 > $BIOS_CTRL
                echo 0x1 > $BIOS_CHIPSELECTION
        elif [ "$1" == "slave" ]; then
                echo 0x2 > $BIOS_CTRL
                echo 0x3 > $BIOS_CHIPSELECTION
        else
                echo "bios_upgrade [master/slave] [flash type] [operation:r/w/e] [file name]"
        fi

        if [ $# == 4 ]; then
                flashrom -p linux_spi:dev=/dev/spidev1.0 -c $2 -$3 $4
        elif [ $# == 3 ] && [ "$3" == "e" ]; then
                flashrom -p linux_spi:dev=/dev/spidev1.0 -c $2 -E
        elif [ $# == 2 ]; then
                flashrom -p linux_spi:dev=/dev/spidev1.0 -c $2
        fi

        gpio_set E4 0
        echo 0x1 > $BIOS_CTRL
        echo 0x1 > $BIOS_CHIPSELECTION
}

come_reset() {
        if [ "$1" == "master" ]; then
                echo 0x0 > $BIOS_CTRL
                echo 0x1 > $BIOS_CHIPSELECTION
                wedge_power.sh cycle
        elif [ "$1" == "slave" ]; then
                echo 0x0 > $BIOS_CTRL
                echo 0x3 > $BIOS_CHIPSELECTION
                wedge_power.sh cycle
        else
                echo "come_reset [master/slave]"
        fi
}

come_boot_info() {
        ((boot_source=$(cat $BIOS_BOOT_CHIP | head -n 1)))
        ((boot_status=$(cat $BIOS_BOOT_STATUS | head -n 1)))

        if [ $boot_status -eq 1 ]; then
                echo "COMe CPU boots OK"
        else
                echo "COMe CPU boots not OK"
        fi

        if [ $boot_source -eq 0 ]; then
                echo "COMe CPU boots from BIOS Master flash"
        else
                echo "COMe CPU boots from BIOS Slave flash"
        fi
}

BCM5387_reset() {
	echo 0 > $SYSLED_BCM5387_RST_SYSFS
	sleep 1
	echo 1 > $SYSLED_BCM5387_RST_SYSFS
}

sys_temp_usage(){
	echo "option: "
	echo "<cpu| switch| optical> #Chip selection"
	echo "<input| max| max_hyst> #Chip temperaure params"
	echo "<#value> #Chip value"
	echo 
}

sys_temp_show() {
	sensors syscpld-i2c-0-0d
}

sys_temp() {
	if [ $# -eq 0 ]; then
		sys_temp_show
		return 0
	fi

	if [ $# -lt 3 ]; then
		sys_temp_usage
		return 1
	fi

	if [ "$1" == "switch" ]; then
		file_prefix="temp1_"
	elif [ "$1" == "cpu" ]; then
		file_prefix="temp2_"
	elif [ "$1" == "optical" ]; then
		file_prefix="temp3_"
	else
		sys_temp_usage
		return 1
	fi

	if [ "$2" == "input" ]; then
		file_suffix="input"
	elif [ "$2" == "max" ]; then
		file_suffix="max"
	elif [ "$2" == "max_hyst" ]; then
		file_suffix="max_hyst"
	else
		sys_temp_usage
		return 1
	fi

	((value=${3}*1000))
	echo $value > $SYSCPLD_SYSFS_DIR/$file_prefix$file_suffix
	return 0
}

come_rest_status() {
	silent=0
	ret=0
	if [ $# -ge 1 ]; then
		silent=$1
	fi

	val=$(cat $COME_RESET_STATUS_SYSFS 2> /dev/null | head -n 1)
	case "$val" in
		0x11)
			info="Power on reset"
	        ret=0 #0x11 powered on reset
			;;
		0x22)
			info="Software trigger CPU to warm reset"
	        ret=1 #0x22 Software trigger CPU warm reset
			;;
		0x33)
			info="Software trigger CPU to cold reset"
	        ret=2 #0x33 Software trigger CPU cold reset
			;;
		0x44)
			info="CPU warm reset"
	        ret=3 #0x44 CPU warm reset
			;;
		0x55)
			info="CPU cold reset"
	        ret=4 #0x55 CPU cold reset
			;;
		0x66)
			info="Watchdog reset"
	        ret=5 #0x66 CPU watchdog reset
			;;
		0x77)
			info="CPU power cycle"
	        ret=6 #0x77 CPU power cycle
			;;
		*)
			info="Power on reset"
			ret=0 #default power on reset
			;;
	esac

	if [ $silent -eq 0 ]; then
		echo "COMe reset status: $info"
	elif [ $silent -eq 2 ]; then
		logger "COMe reset status: $info"
	fi

	return $ret
}
