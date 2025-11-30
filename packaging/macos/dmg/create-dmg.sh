#!/bin/bash
# Create DMG installer for simple-nfsd
# Usage: ./create-dmg.sh [version]

set -e

PROJECT_NAME="simple-nfsd"
VERSION="${1:-1.0.0}"
DMG_NAME="$simple-nfsd-${VERSION}"
DMG_TEMP="dmg-temp"
DMG_FILE="${DMG_NAME}.dmg"
APP_NAME="$simple-nfsd.app"
PKG_NAME="$simple-nfsd.pkg"

# Clean up previous builds
rm -rf "$DMG_TEMP" "$DMG_FILE"

# Create temporary directory structure
mkdir -p "$DMG_TEMP"

# Copy application or package
if [ -f "$PKG_NAME" ]; then
    cp "$PKG_NAME" "$DMG_TEMP/"
elif [ -d "$APP_NAME" ]; then
    cp -R "$APP_NAME" "$DMG_TEMP/"
fi

# Copy license and readme
cp ../licenses/LICENSE.txt "$DMG_TEMP/LICENSE.txt" 2>/dev/null || true
cp ../assets/readme.html "$DMG_TEMP/README.html" 2>/dev/null || true

# Create Applications symlink
ln -s /Applications "$DMG_TEMP/Applications"

# Create DMG
hdiutil create -volname "$PROJECT_NAME" -srcfolder "$DMG_TEMP" -ov -format UDZO "$DMG_FILE"

# Add license and icon
if [ -f "../assets/icons/$simple-nfsd.icns" ]; then
    # Set custom icon (requires additional tools)
    echo "Custom icon available at ../assets/icons/$simple-nfsd.icns"
fi

# Clean up
rm -rf "$DMG_TEMP"

echo "✓ Created $DMG_FILE"

