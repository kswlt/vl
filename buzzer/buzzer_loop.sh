#!/bin/sh
echo 23 > /sys/class/gpio/unexport 2>/dev/null
echo 22 > /sys/class/gpio/unexport 2>/dev/null
while true; do
  /userdata/xtapp/buzzer_tone 2700 120 23
done
