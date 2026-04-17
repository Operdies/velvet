#!/usr/bin/env bash

VV="./debug/vv"
make $VV
echo 'return arg' | $VV lua
echo 'return arg' | $VV lua - "test one" 1
echo 'return arg' | $VV lua -- "test two" 2

$VV lua <(echo 'return arg') "test three" 3
$VV lua < <(echo 'return arg') -- "test four" 4
$VV lua -- "test five" 5 < <(echo 'return arg')
$VV lua -- "test six" 6 <<< 'return arg'
$VV lua - "test seven" 7 <<< 'return arg'

$VV lua <<< 'print("Test Eight", 8)'
$VV lua - <<< 'print("Test Nine", 9)'
$VV lua -- <<< 'print("Test Ten", 10)'
