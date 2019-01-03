#!/bin/bash
((MAX_OUTPUT = 930))
((MIN_OUTPUT = 720))
((MAX_VALUE = 0xB2))
((MIN_VALUE = 0x02))

((OUTPUT_BASE = 50000))
((BASE_VALUE = $MAX_VALUE))
((DELTA_OUTPUT = 625))

FPGA_BUS=5
FPGA_ADDR=0x36
AVS_ADDR=0x30
i2cset -f -y $FPGA_BUS $FPGA_ADDR 0x0 $AVS_ADDR
((value = $(i2cget -f -y 5 0x36 0x30) ))
# ((value = (value & 0xff00) >> 8))
if [ $value -gt $MAX_VALUE ]; then
    ((value=$MAX_VALUE))
elif [ $value -lt $MIN_VALUE ]; then
    ((value=$MIN_VALUE))
fi

((output = ($BASE_VALUE - $value) * $DELTA_OUTPUT + $OUTPUT_BASE))
((output_voltage = $output/100 ))
if [ $output_voltage -gt $MAX_OUTPUT ]; then
    ((output_voltage=$MAX_OUTPUT))
elif [ $output_voltage -lt $MIN_OUTPUT ]; then
    ((output_voltage=$MIN_OUTPUT))
fi

echo "Set AVS Voltage: $output_voltage mv"
i2cset -f -y 17 0x60 0x21 $output_voltage w
