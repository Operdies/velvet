#!/usr/bin/env bash

MAX="$1"
for ((i=0;i<=$MAX;i++)); do
  printf "Line %d of %d\n" $i $MAX
done
