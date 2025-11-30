# Installer Icons and Graphics

This directory should contain icon files and graphics for installers.

## Required Files

### Windows
- **{PROJECT_NAME}.ico** - Application icon (256x256, ICO format)
- **header.bmp** - NSIS header image (150x57, BMP format)
- **wizard.bmp** - NSIS wizard image (164x314, BMP format)
- **wix-banner.bmp** - WiX banner image (493x58, BMP format)
- **wix-dialog.bmp** - WiX dialog image (493x312, BMP format)

### macOS
- **{PROJECT_NAME}.icns** - Application icon (512x512, ICNS format)
- **background.png** - PKG background image (620x418, PNG format)
- **dmg-background.png** - DMG background image (658x498, PNG format)

## Creating Icons

### Windows ICO
```bash
# Convert PNG to ICO
convert icon.png -resize 256x256 icon.ico
```

### macOS ICNS
```bash
# Create ICNS from PNG
iconutil -c icns icon.iconset
```

### Creating Icon Sets
1. Start with a high-resolution PNG (1024x1024 or larger)
2. Create platform-specific formats
3. Ensure icons are recognizable at small sizes
4. Use consistent branding across all icons

## Notes
- Icons should be professional and represent the application
- Use consistent color schemes
- Test icons at various sizes
- Ensure icons work on both light and dark backgrounds

