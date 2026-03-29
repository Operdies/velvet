#!/usr/bin/env bash

dd if=/dev/urandom count=200 bs=4M 2> /tmp/base64-cat | base64
