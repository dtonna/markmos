#!/usr/bin/env bash
set -euo pipefail

# ─── iOS Production Build ─────────────────────────────────────────
# Creates an archived .xcarchive → exports signed .ipa for App Store
#
# Usage:
#   DEVELOPMENT_TEAM=ABCDE12345 ./ios_build.sh
#   DEVELOPMENT_TEAM=ABCDE12345 CONFIG=Release ./ios_build.sh
#
# Prerequisites:
#   - Apple Developer membership + distribution cert + App Store profile
#   - DEVELOPMENT_TEAM env var (Team ID from developer.apple.com)

cd "$(dirname "$0")"

CONFIG="${CONFIG:-Release}"
BUILD_DIR="build-ios-prod"
ARCHIVE_PATH="$BUILD_DIR/markmos.xcarchive"
EXPORT_DIR="$BUILD_DIR/ipa"
EXPORT_OPTIONS="$BUILD_DIR/export-options.plist"

if [ -z "${DEVELOPMENT_TEAM:-}" ]; then
    echo "ERROR: DEVELOPMENT_TEAM not set"
    echo "  Find your Team ID at https://developer.apple.com/account (Membership tab)"
    exit 1
fi

# 1. CMake configure
cmake -G Xcode \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM" \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTS=OFF \
    -DENGINE_ENABLE_ASSERT=OFF \
    -S . -B "$BUILD_DIR"

# 2. Archive
xcodebuild -project "$BUILD_DIR/markmos.xcodeproj" \
    -scheme markmos_game \
    -configuration "$CONFIG" \
    -archivePath "$ARCHIVE_PATH" \
    -destination generic/platform=iOS \
    -derivedDataPath "$BUILD_DIR/DerivedData" \
    archive

# 3. Generate export-options.plist
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

# 4. Export .ipa
xcodebuild -exportArchive \
    -archivePath "$ARCHIVE_PATH" \
    -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist "$EXPORT_OPTIONS"

echo "✅ IPA exported: $EXPORT_DIR/markmos_game.ipa"
echo "   Upload via: Xcode Organizer → Distribute App → App Store"
echo "   Or: xcrun altool --upload-app -f \"$EXPORT_DIR/markmos_game.ipa\" -t ios"
