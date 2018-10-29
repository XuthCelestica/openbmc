#!/bin/sh

source /usr/local/fbpackages/utils/ast-functions

devmem_clear_bit 0x1e6e2090 5
devmem_clear_bit 0x1e6e2090 4
devmem_clear_bit 0x1e6e2088 8
devmem_set_bit 0x1e78007c 16
devmem_set_bit 0x1e780078 16

if [ ! -f /dev/spidev2.0 ]
then
	mknod /dev/spidev2.0 c 153 0
	modprobe spidev
fi
flashrom -p linux_spi:dev=/dev/spidev2.0 -c W25Q64.V > /dev/null
#flashrom -p linux_spi:dev=/dev/spidev5.0 -r bios
