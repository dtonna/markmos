#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build-ios}"
CONFIG="${CONFIG:-Debug}"
TARGET="${TARGET:-markmos_game}"
SDK="${SDK:-iphoneos}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-16.0}"

cmake_args="
    -G Xcode
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_OSX_SYSROOT=$SDK
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET
"

if [ "${DEVELOPMENT_TEAM:-}" != "" ]; then
    cmake_args="$cmake_args -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=$DEVELOPMENT_TEAM"
fi

# Examples:
#   ./build-ios.sh
#   SDK=iphonesimulator TARGET=mm_05_particle_stress ./build-ios.sh
#   DEVELOPMENT_TEAM=ABCDE12345 CONFIG=Release ./build-ios.sh
cmake $cmake_args -S . -B "$BUILD_DIR"

cmake --build "$BUILD_DIR" \
    --config "$CONFIG" \
    --target "$TARGET"
