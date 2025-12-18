#!/usr/bin/env sh
vv source - << EOF
map "<C-w>" detach ; map "<C-a>" spawn bash
map "<C-b>" spawn zsh
EOF
