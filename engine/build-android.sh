#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_DIR="$(cd "$SCRIPT_DIR" && pwd)"
PROJECT_DIR="$(cd "$ENGINE_DIR/../project/android" && pwd)"

if [ -z "${ANDROID_NDK_HOME:-}" ] && [ -z "${ANDROID_NDK:-}" ]; then
    echo "ERROR: ANDROID_NDK_HOME not set"
    exit 1
fi

CONFIG="${CONFIG:-Debug}"

cd "$PROJECT_DIR"
./gradlew "assemble${CONFIG}" --parallel
