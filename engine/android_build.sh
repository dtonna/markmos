#!/usr/bin/env bash
set -euo pipefail

# ─── Android Production Build ──────────────────────────────────────
# Assembles a signed .aab (Android App Bundle) for Play Store
#
# Usage:
#   ./android_build.sh
#   ./android_build.sh release  (same as default)
#   ./android_build.sh debug
#
# Prerequisites:
#   - ANDROID_NDK_HOME or NDK in SDK
#   - keystore for signing (set in gradle.properties or env)

cd "$(dirname "$0")/../project/android"

BUILD_TYPE="${1:-release}"

case "$BUILD_TYPE" in
    release)
        ./gradlew bundleRelease
        echo "✅ AAB: app/build/outputs/bundle/release/app-release.aab"
        echo "   Upload to Google Play Console"
        ;;
    debug)
        ./gradlew assembleDebug
        echo "✅ APK: app/build/outputs/apk/debug/app-debug.apk"
        echo "   Install: adb install app/build/outputs/apk/debug/app-debug.apk"
        ;;
    *)
        echo "Usage: $0 [release|debug]"
        exit 1
        ;;
esac
