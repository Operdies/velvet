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

exec "$DIR/.venv/bin/python" "$DIR/server.py" "$PORT"
