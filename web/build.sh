#!/usr/bin/env sh
podman build --build-arg VELVET_VERSION="$(git describe --tags --always)" -t velvet -f web/Dockerfile .
