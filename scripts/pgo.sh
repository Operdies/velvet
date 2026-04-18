#!/bin/sh
set -e

PGO_DIR="$(pwd)/pgo"
PROFILE_DIR="$PGO_DIR/profiles"

rm -rf "$PGO_DIR"
mkdir -p "$PROFILE_DIR"

echo "=== Step 1: Building instrumented binary ==="
make j10 -B release EXTRA_CFLAGS="-fprofile-generate=$PROFILE_DIR" EXTRA_LDFLAGS="-fprofile-generate=$PROFILE_DIR"

echo ""
echo "=== Step 2: Run the instrumented binary ==="
echo "=== Perform your workload, then exit normally ==="
echo ""
./release/vv "$@"

echo ""
echo "=== Step 3: Building optimized binary with profile data ==="
make -j10 -B release EXTRA_CFLAGS="-fprofile-use=$PROFILE_DIR -fprofile-correction" EXTRA_LDFLAGS="-fprofile-use=$PROFILE_DIR"

rm -rf "$PGO_DIR"

echo ""
echo "=== Done. PGO-optimized binary at ./release/vv ==="
