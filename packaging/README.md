# Packaging Templates

This directory contains templates for creating installers and packages for {PROJECT_NAME} across different platforms.

## Directory Structure

```
packaging/
├── macos/
│   ├── pkg/                    # macOS PackageMaker (PKG) installer
│   │   ├── Distribution.xml     # Package distribution configuration
│   │   ├── PackageInfo.xml      # Package metadata
│   │   └── scripts/
│   │       └── postinstall      # Post-installation script
│   └── dmg/                     # macOS Disk Image (DMG)
│       └── create-dmg.sh        # DMG creation script
├── windows/
│   ├── nsis/                    # NSIS installer
│   │   └── installer.nsi        # NSIS installer script
│   └── msi/                     # Windows Installer (MSI)
│       └── installer.wxs        # WiX installer script
├── linux/
│   ├── deb/                     # Debian/Ubuntu packages
│   │   ├── control              # Debian control file
│   │   └── postinst             # Post-installation script with license
│   └── rpm/                     # Red Hat/CentOS packages
│       └── {PROJECT_NAME}.spec  # RPM spec file with license
├── assets/
│   ├── icons/                   # Installer icons and graphics
│   │   ├── {PROJECT_NAME}.ico   # Windows icon
│   │   ├── {PROJECT_NAME}.icns  # macOS icon
│   │   ├── header.bmp           # NSIS header image
│   │   ├── wizard.bmp           # NSIS wizard image
│   │   ├── background.png       # PKG background
│   │   └── dmg-background.png   # DMG background
│   ├── welcome.html             # Welcome page for PKG
│   ├── readme.html              # Read me page
│   └── conclusion.html          # Installation complete page
└── licenses/
    ├── LICENSE.txt              # Plain text license
    └── LICENSE.rtf               # Rich text license for Windows
```

## Features

### License Acceptance
- **macOS PKG**: License displayed and must be accepted
- **Windows NSIS/MSI**: License page with acceptance required
- **Linux DEB/RPM**: License displayed during installation with acceptance prompt

### Custom Icons and Graphics
- Windows: `.ico` files for application and installer
- macOS: `.icns` files for application, PNG for backgrounds
- Custom header/wizard images for NSIS installers
- DMG background images

### Platform-Specific Features

#### macOS PKG
- Modern installer with welcome/readme/conclusion pages
- License acceptance
- Post-installation scripts
- Service user creation
- LaunchDaemon integration

#### macOS DMG
- Custom background image
- Applications symlink
- License and README included
- Compressed format (UDZO)

#### Windows NSIS
- Modern UI with custom graphics
- License acceptance page
- Component selection
- Start Menu shortcuts
- Windows Service installation
- Uninstaller included

#### Windows MSI
- WiX-based installer
- License acceptance
- Service installation
- Registry entries
- Upgrade support

#### Linux DEB
- License acceptance during installation
- Service user creation
- Systemd integration
- Configuration file installation

#### Linux RPM
- License acceptance in %pre section
- Service user creation
- Systemd integration
- Proper file placement

## Usage

### Building Packages

#### macOS PKG
```bash
# Build the project first
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Create package
cpack -G PackageMaker
```

#### macOS DMG
```bash
# After creating PKG
cd packaging/macos/dmg
./create-dmg.sh 1.0.0
```

#### Windows NSIS
```bash
# Build NSIS installer
makensis packaging/windows/nsis/installer.nsi
```

#### Windows MSI
```bash
# Build with WiX
candle packaging/windows/msi/installer.wxs
light installer.wixobj
```

#### Linux DEB
```bash
# Build Debian package
dpkg-buildpackage -us -uc
```

#### Linux RPM
```bash
# Build RPM package
rpmbuild -ba packaging/linux/rpm/{PROJECT_NAME}.spec
```

## Template Variables

Replace these placeholders in templates:
- `{PROJECT_NAME}` - Project name (e.g., simple-dhcpd)
- `{PROJECT_USER}` - Service user name (e.g., dhcpdev)
- `{PROJECT_GROUP}` - Service group name
- `{PROTOCOL}` - Protocol name (e.g., DHCP, NTP)
- `${VERSION}` - Version number (set during build)

## Icon Requirements

### Windows
- **Application Icon**: 256x256, `.ico` format
- **Header Image**: 150x57, `.bmp` format
- **Wizard Image**: 164x314, `.bmp` format

### macOS
- **Application Icon**: 512x512, `.icns` format
- **Background**: 620x418, `.png` format
- **DMG Background**: 658x498, `.png` format

## License Files

- **LICENSE.txt**: Plain text license (for all platforms)
- **LICENSE.rtf**: Rich text format license (for Windows MSI)

## Notes

- All scripts must be executable (`chmod +x`)
- License files must be present in `packaging/licenses/`
- Icon files must be present in `packaging/assets/icons/`
- Update GUIDs in MSI/WiX templates with unique values
- Test installers on clean systems before distribution

