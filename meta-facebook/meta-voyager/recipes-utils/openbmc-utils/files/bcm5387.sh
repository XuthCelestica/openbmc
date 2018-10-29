#!/bin/sh

source /usr/local/fbpackages/utils/ast-functions

gpio_set R6 1
gpio_set R7 1
devmem_clear_bit 0x1e6e2090 5
devmem_clear_bit 0x1e6e2090 4
devmem_clear_bit 0x1e6e2088 10
devmem_set_bit 0x1e78007c 18
devmem_set_bit 0x1e780078 18
i2cset -f -y 13 0x31 0x14 0xfb
sleep 1
i2cset -f -y 13 0x31 0x14 0xff
bcm5396_util.py $@
