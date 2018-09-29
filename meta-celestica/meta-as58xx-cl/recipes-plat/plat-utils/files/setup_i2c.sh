#!/bin/sh
#
# Copyright 2018-present Facebook. All Rights Reserved.
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

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/bin

. /usr/local/bin/openbmc-utils.sh

board_type=$(board_type)

# Bus 0
i2c_device_add 0 0x0d syscpld  #CPLD

# Bus 1
i2c_device_add 1 0x50 24c32  #CPU EEPROM

# Bus 2
i2c_device_add 2 0x51 24c64  #Switch EEPROM
i2c_device_add 2 0x53 24c64  #BMC EEPROM
i2c_device_add 2 0x57 24c64  #SYS EEPROM

# Bus 3

# Bus 4
i2c_device_add 4 0x15 ir3584  #IR3584 in COMe
i2c_device_add 4 0x16 ir3584  #IR3584 in COMe
i2c_device_add 4 0x35 max11617 #Max11617 ADC
i2c_device_add 4 0x43 ir38060  #IR38060
i2c_device_add 4 0x49 ir38062  #IR38062

# Bus 5
#i2c_device_add 5 0x36 fpga    #FPGA

# Bus 6
#PCA9548A

# Bus 7
i2c_device_add 7 0x4a tmp75 #temp
i2c_device_add 7 0x4b tmp75
i2c_device_add 7 0x4c tmp75
i2c_device_add 7 0x4d tmp75

# Bus 8
i2c_device_add 8 0x0d fancpld #Fan CPLD



###################################################
# Bus 16 for i2c-4 PCA9548
#bus 16 channel 0
i2c_device_add 16 0x12 ir3595  #IR3595
#bus 17 channel 1
i2c_device_add 17 0x47 ir38060  #IR38060
#bus 18 channel 2
i2c_device_add 18 0x70 ir3584  #IR3584 channel 0
i2c_device_add 18 0x71 ir3584  #IR3584 channel 1
#bus 19 channel 3
#bus 20 channel 4
#bus 21 channel 5
#bus 22 channel 6
#bus 23 channel 7

# Bus 24 for i2c-6 PCA9548
#bus 24 channel 0
i2c_device_add 24 0x50 24c32 #PSU1 FRU
i2c_device_add 24 0x58 24c32 #PSU1 PMBUS
#bus 25 channel 1
i2c_device_add 25 0x51 psu_fru #PSU2 FRU
i2c_device_add 25 0x59 dps1100 #PSU2 PMBUS
#bus 26 channel 2
#bus 27 channel 3
#bus 28 channel 4
#bus 29 channel 5
#bus 30 channel 6
#bus 31 channel 7


# Bus 32 for i2c-8 PCA9548
#bus 32 channel 0
i2c_device_add 32 0x50 24c64 #Fan EEPROM
#bus 33 channel 1
#bus 34 channel 2
i2c_device_add 34 0x50 24c64
#bus 35 channel 3
#bus 36 channel 4
i2c_device_add 36 0x50 24c64
#bus 37 channel 5
#bus 38 channel 6
i2c_device_add 38 0x50 24c64
#bus 39 channel 7
i2c_device_add 39 0x56 24c64 #Fan Board EEPROM
i2c_device_add 39 0x48 tmp75
i2c_device_add 39 0x49 tmp75

#For Phalanx Board
if [ "$board_type" = "Phalanx" ]; then
	#bus 36 channel 4 For Fan 5
	i2c_device_add 36 0x50 24c64


	# Bus 16 for i2c-4 PCA9548
	#bus 19 channel 3
	#i2c_device_add 19 0x41 ir38060  #IR38060
	#bus 20 channel 4
	#i2c_device_add 20 0x41 ir38060  #IR38060


	# Bus 40 for i2c-2 PCA9548
	i2c_device_add 2 0x77 pca9548 #PCA9548
	#bus 40 channel 0 For LC EEPROM
	i2c_device_add 40 0x50 24c64 #LC1

	#bus 41 channel 1 For LC EEPROM
	i2c_device_add 41 0x54 24c64 #LC2


	# Bus 48 for i2c-7 PCA9548
	i2c_device_add 7 0x77 pca9548 #PCA9548
	#bus 48 channel 0 For Temp
	i2c_device_add 48 0x48 tmp75
	i2c_device_add 48 0x49 tmp75

	#bus 49 channel 1 For Temp
	i2c_device_add 49 0x48 tmp75
	i2c_device_add 49 0x49 tmp75
fi


# run sensors.config set command
sensors -s
