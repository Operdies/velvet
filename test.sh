#!/usr/bin/env bash

cd "$(dirname "$0")"

echo =========== Build Debug ==============
make -j
echo =========== Build Release ============
make -j release
echo =========== Test Debug ===============
./bin/test
echo =========== Test Release =============
./release/test
