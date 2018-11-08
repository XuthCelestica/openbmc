#!/bin/bash
. /usr/local/bin/openbmc-utils.sh
board_type=$(board_type)
if [ "$board_type" = "Phalanx" ]; then
	mv /etc/eeprom-data/phalanx.xml /etc/eeprom-data/eeprom.xml
	rm /etc/eeprom-data/fishbone.xml
else
	mv /etc/eeprom-data/fishbone.xml /etc/eeprom-data/eeprom.xml
	rm /etc/eeprom-data/phalanx.xml
fi

exit 0
