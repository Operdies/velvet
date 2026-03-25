#!/bin/sh
vv foreground -S /tmp/vv.sock &
n=0
while [ ! -e /tmp/vv.sock ]; do
  n=$((n + 1))
  [ "$n" -gt 100 ] && echo "velvet server failed to start" >&2 && exit 1
  sleep 0.05
done
exec vv attach -S /tmp/vv.sock
