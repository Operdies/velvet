#!/usr/bin/env bash

cd "$(dirname "$0")"

set -euo pipefail

echo =========== Build Debug ==============
make -j
echo =========== Test Debug ===============
./bin/test
echo =========== Build Release ============
make -j release
echo =========== Test Release =============
./release/test
