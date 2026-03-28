#!/bin/bash
set -e

PORT=${1:-3000}
unset VELVET VELVET_WINID
DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -d "$DIR/.venv" ]; then
    echo "Setting up virtual environment..."
    python3 -m venv "$DIR/.venv"
    "$DIR/.venv/bin/pip" install -q websockets
fi

# allow velvet sessions to use at most 14/16 logical cores.
# individual pods are further limited to 120% which appears to be a sweetspot for velvet throughput
# (server + client relay)
if command -v systemd-run; then
  systemd-run --user --scope --property="CPUQuota=1400%" -- "$DIR/.venv/bin/python" "$DIR/server.py" "$PORT"
else
  "$DIR/.venv/bin/python" "$DIR/server.py" "$PORT"
fi
