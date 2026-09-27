#!/bin/sh
echo 23 > /sys/class/gpio/unexport 2>/dev/null
for f in 2900 2950 3000 3050 3100 3150 3200 3250 3300; do
  echo FREQ=$f
  /userdata/xtapp/buzzer_tone $f 2 23
done
echo ALLDONE
