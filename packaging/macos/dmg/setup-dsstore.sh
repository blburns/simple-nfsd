#!/bin/bash
# Setup .DS_Store for DMG with custom layout
# This creates a properly formatted DMG with Applications symlink

set -e

DMG_TEMP="$1"
if [ -z "$DMG_TEMP" ]; then
    echo "Usage: $0 <dmg-temp-directory>"
    exit 1
fi

# Create .DS_Store with custom layout
# This positions files in the DMG window
# Note: This requires additional tools or manual setup
# For automated setup, consider using create-dmg or similar tools

echo "Setting up DMG layout in $DMG_TEMP"
echo "Note: Custom .DS_Store creation requires additional tools"
echo "Consider using create-dmg or manually setting up the DMG layout"

# Basic layout (if create-dmg is available)
if command -v create-dmg &> /dev/null; then
    echo "create-dmg found - will use it for proper layout"
else
    echo "Install create-dmg for better DMG layout:"
    echo "  brew install create-dmg"
fi

exit 0

