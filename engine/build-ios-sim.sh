#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build-ios-sim}"
CONFIG="${CONFIG:-Debug}"
TARGET="${TARGET:-markmos_game}"
SDK="${SDK:-iphonesimulator}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-17.0}"
ARCH="${ARCH:-arm64}"

cmake -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=$SDK \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET \
    -DCMAKE_OSX_ARCHITECTURES=$ARCH \
    -S "$SCRIPT_DIR" -B "$BUILD_DIR"

cmake --build "$BUILD_DIR" \
    --config "$CONFIG" \
    --target "$TARGET" \
    --
