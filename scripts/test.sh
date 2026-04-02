#!/usr/bin/env bash

cd "$(dirname "$0")/.."

set -euo pipefail

echo =========== Build Debug ==============
make -j8 debug
echo =========== Test Debug ===============
./debug/test
echo =========== Build Release ============
make -j8 release
echo =========== Test Release =============
./release/test
