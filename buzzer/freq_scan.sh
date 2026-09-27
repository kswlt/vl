#!/bin/sh
echo 23 > /sys/class/gpio/unexport 2>/dev/null
echo 22 > /sys/class/gpio/unexport 2>/dev/null
for f in 2400 2500 2600 2700 2800 2900 3000 3100; do
  echo FREQ=$f
  /userdata/xtapp/buzzer_tone $f 4 23
done
echo ALLDONE
