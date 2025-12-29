#!/usr/bin/env bash

echo =========== Build Debug ==============
make -j
echo =========== Build Release ============
make -j release
echo =========== Test Debug ===============
./bin/test
echo =========== Test Release =============
./release/test
