# Simple Dummy Application

A test application for demonstrating the standardized build system and static binary creation capabilities.

## Features

- Simple daemon/foreground execution modes
- Threaded worker implementation
- Configurable logging
- Cross-platform build support
- Static binary generation

## Building

```bash
# Regular build
make build

# Static binary build
make static-build

# Create packages
make package
make static-package
```

## Usage

```bash
# Run in foreground
./simple-dummy

# Run as daemon
./simple-dummy --daemon
```

## Configuration

Copy `config/simple-dummy.conf.example` to your desired location and modify as needed.

## Static Binary

This application supports static binary creation for easy distribution:

```bash
make static-package
```

This creates platform-specific packages containing the self-contained executable.
