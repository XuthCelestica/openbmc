#!/bin/sh
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

### BEGIN INIT INFO
# Provides:          setup-fan
# Required-Start:    board-id
# Required-Stop:
# Default-Start:     S
# Default-Stop:
# Short-Description: Set fan speed
### END INIT INFO

echo "Setup fan speed... "
/usr/local/bin/set_fan_speed.sh 50
cp /etc/pid_config.ini /mnt/data/
cp /etc/pid_config_v2.ini /mnt/data/
brd_type=$(cat /sys/bus/i2c/devices/0-000d/sw_brd_type | head -n 1)
if [ $brd_type == 0x1 ]; then
    echo "Run fand_v2"
    /usr/local/bin/fand_v2
elif [ $brd_type == 0x0 ]; then
    echo "Run fand32_v2"
    /usr/local/bin/fand32_v2
else
    echo "Run default:fand_v2"
    /usr/local/bin/fand_v2
fi
echo "done."
