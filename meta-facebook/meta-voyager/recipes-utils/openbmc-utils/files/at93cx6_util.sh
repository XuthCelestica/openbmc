#!/bin/sh

source /usr/local/fbpackages/utils/ast-functions

gpio_set R2 1
gpio_set R3 1
gpio_set R4 1
gpio_set R5 1
devmem_clear_bit 0x1e6e2090 4
devmem_clear_bit 0x1e6e2090 5
devmem_clear_bit 0x1e6e2088 10

devmem_clear_bit 0x1e6e2088 26
devmem_clear_bit 0x1e6e2088 27
devmem_clear_bit 0x1e6e2088 28
devmem_clear_bit 0x1e6e2088 29

devmem_set_bit 0x1e78007c 18
devmem_set_bit 0x1e780078 18

at93cx6_util.py $@
