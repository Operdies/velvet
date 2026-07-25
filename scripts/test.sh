#!/usr/bin/env bash

cd "$(dirname "$0")/.."
MODE="$1"

set -euo pipefail

function debug() {
  make -j8 debug
  echo =========== Test Debug ===============
  ./debug/test
}

function release() {
  make -j8 release
  echo =========== Test Release =============
  ./release/test
}

if [[ "$MODE" = "debug" ]]; then
  debug
fi

if [[ "$MODE" = "release" ]]; then
  release
fi

if [[ "$MODE" = "" ]]; then
  debug
  release
fi
