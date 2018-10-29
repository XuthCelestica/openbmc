# Copyright 2015-present Facebook. All Rights Reserved.

SYSCPLD_SYSFS_DIR="/sys/bus/i2c/devices/i2c-13/13-0031"
PWR_MAIN_SYSFS="${SYSCPLD_SYSFS_DIR}/pwr_main_n"
SCM_PRESENT_SYSFS="${SYSCPLD_SYSFS_DIR}/micro_srv_present"
PWR_BOARD_VER="${SYSCPLD_SYSFS_DIR}/board_ver"
SLOTID_SYSFS="${SYSCPLD_SYSFS_DIR}/slotid"
PWR_USRV_SYSFS="${SYSCPLD_SYSFS_DIR}/pwr_come_en"

voyager_iso_buf_enable() {
    # TODO, no isolation buffer
    return 0
}

voyager_iso_buf_disable() {
    # TODO, no isolation buffer
    return 0
}

voyager_is_us_on() {
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
    val=$(cat $PWR_USRV_SYSFS 2> /dev/null | head -n 1)
    if [ -z "$val" ]; then
        return $default
    elif [ "$val" == "0x1" ]; then
        return 0            # powered on
    else
        return 1
    fi
}

voyager_board_type() {
    echo 'Voyager'
}

voyager_slot_id() {
    #printf "%d\n" $(cat $SLOTID_SYSFS)
	return 0
}

voyager_board_rev() {
    local val
    val=$(cat $PWR_BOARD_VER 2> /dev/null | head -n 1)
    echo $val
}

# Should we enable OOB interface or not
voyager_should_enable_oob() {
    # voyager100 uses BMC MAC since beginning
    return -1
}

voyager_power_on_board() {
    local val present
    # power on main power, uServer power
    val=$(cat $PWR_MAIN_SYSFS | head -n 1)
    if [ "$val" != "0x1" ]; then
        echo 1 > $PWR_MAIN_SYSFS
        sleep 2
    fi
}

voyager_power_off_board() {
    # power off main power, uServer power
    echo 0 > $PWR_MAIN_SYSFS
    sleep 2
}
