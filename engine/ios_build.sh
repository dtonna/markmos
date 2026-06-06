#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

CONFIG="${CONFIG:-Release}"
BUILD_DIR="$SCRIPT_DIR/build-ios-prod"
ARCHIVE_PATH="$BUILD_DIR/markmos.xcarchive"
EXPORT_DIR="$BUILD_DIR/ipa"
EXPORT_OPTIONS="$BUILD_DIR/export-options.plist"

if [ -z "${DEVELOPMENT_TEAM:-}" ]; then
    echo "ERROR: DEVELOPMENT_TEAM not set"
    echo "  Find your Team ID at https://developer.apple.com/account"
    exit 1
fi

cmake -G Xcode \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM" \
    -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DENGINE_ENABLE_ASSERT=OFF \
    -S "$SCRIPT_DIR" -B "$BUILD_DIR"

xcodebuild -project "$BUILD_DIR/markmos.xcodeproj" \
    -scheme markmos_game \
    -configuration "$CONFIG" \
    -archivePath "$ARCHIVE_PATH" \
    -destination generic/platform=iOS \
    -derivedDataPath "$BUILD_DIR/DerivedData" \
    archive

cat > "$EXPORT_OPTIONS" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>app-store</string>
    <key>teamID</key>
    <string>$DEVELOPMENT_TEAM</string>
    <key>uploadSymbols</key>
    <true/>
    <key>uploadBitcode</key>
    <false/>
    <key>compileBitcode</key>
    <false/>
</dict>
</plist>
EOF

xcodebuild -exportArchive \
    -archivePath "$ARCHIVE_PATH" \
    -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist "$EXPORT_OPTIONS"

echo "✅ IPA exported: $EXPORT_DIR/markmos_game.ipa"
