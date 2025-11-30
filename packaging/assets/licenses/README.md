# License Files

This directory should contain license files in various formats for installers.

## Required Files

### LICENSE.txt
Plain text license file used by:
- macOS PKG installer
- Linux DEB/RPM packages
- General distribution

### LICENSE.rtf
Rich text format license file used by:
- Windows MSI (WiX) installer
- Windows NSIS installer (optional)

## Converting License to RTF

If you have a plain text license, you can convert it to RTF:

```bash
# Using pandoc
pandoc LICENSE.txt -o LICENSE.rtf

# Or manually create RTF with proper formatting
```

## License Content

The license file should include:
- Full license text (Apache 2.0, MIT, GPL, etc.)
- Copyright notice
- Terms and conditions
- Warranty disclaimers

## Notes
- Keep license files up to date
- Ensure license matches your project's actual license
- Test license display in all installers
- Make sure license is readable and properly formatted

