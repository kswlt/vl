#!/bin/sh
# Alarm only after the gateway reports a persistent sensor stream failure.
STATUS_URL=http://127.0.0.1:8080/api/management/status
BM=/userdata/xtapp/buzzer_morse
LOG=/tmp/mm.log

candidate=
streak=0
active=
last_alarm=0
trap 'exit 0' TERM INT

# Give the gateway and sensors time to start after power-on.
sleep 20
while :; do
  status=$(wget -qO- -T 3 "$STATUS_URL" 2>/dev/null)
  cam=$(printf '%s' "$status" | sed -n 's/.*"sensors":{"camera":{[^}]*"healthy":\(true\|false\).*/\1/p')
  radar=$(printf '%s' "$status" | sed -n 's/.*"lidar":{[^}]*"healthy":\(true\|false\).*/\1/p')
  if [ -z "$cam" ] || [ -z "$radar" ]; then
    # A failed status request is not evidence that either sensor stopped.
    sleep 2
    continue
  fi

  code=
  if [ "$cam" = false ] && [ "$radar" = false ]; then code=RC
  elif [ "$cam" = false ]; then code=C
  elif [ "$radar" = false ]; then code=R
  fi

  if [ "$code" = "$candidate" ]; then streak=$((streak + 1))
  else candidate=$code; streak=1
  fi

  if [ -z "$code" ]; then
    if [ -n "$active" ]; then
      echo "[ok] $(date +%H:%M:%S) recovered from $active" >> "$LOG"
    fi
    active=
  elif [ "$streak" -ge 3 ]; then
    now=$(date +%s)
    if [ "$code" != "$active" ] || [ $((now - last_alarm)) -ge 20 ]; then
      echo "[alarm] $(date +%H:%M:%S) state=$code camera=$cam lidar=$radar" >> "$LOG"
      "$BM" "$code"
      active=$code
      last_alarm=$(date +%s)
    fi
  fi
  sleep 2
done
