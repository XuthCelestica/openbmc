#!/bin/sh

a=-0.00253
b=-10.339
c=1894.3
vol=$(cat /sys/devices/platform/ast_adc.0/in12_input 2> /dev/null)
vol=$(($vol*18/10))
Vtemp=$vol
b2=$(echo "$b*$b" | bc)
#echo "b2=$b2"
t1=$(echo "$c-$Vtemp" |bc)
#echo "t1(c-Vtemp)=$t1"
t2=$(echo "4*$a*$t1" |bc)
#echo "t2(4a(c-Vtemp)=$t2"
t3=$(echo "sqrt($b2+(-1)*$t2)" |bc)
#echo "t3(sqrt(b2-4a(c-Vtemp))=$t3"
t4=$(echo "-1*$b-$t3" |bc)
#echo "t4=$t4"
t5=$(echo "$t4/$a/2" |bc)
#echo "t5=$t5"
temp=$(echo "$t5 + 30" |bc)
echo "$temp"
