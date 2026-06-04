#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build-xcode}"
CONFIG="${CONFIG:-Debug}"
TARGET="${TARGET:-markmos_game}"

cmake -G Xcode \
    -S . \
    -B "$BUILD_DIR"

cmake --build "$BUILD_DIR" \
    --config "$CONFIG" \
    --target "$TARGET"
