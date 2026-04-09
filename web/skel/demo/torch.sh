#!/usr/bin/env sh

# This script demonstrates how overlays can be used to draw user attention.

DIR="$(cd "$(dirname "$0")" && pwd)"
TORCH="$DIR/torch.lua"
exec vv lua < "$TORCH"
