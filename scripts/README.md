# Build Scripts

This directory contains platform-specific build scripts for {{PROJECT_NAME}} that automate the build and installation process.

## Available Scripts

### Linux Scripts

#### `build-linux.sh` - Generic Linux Build Script
**Recommended for most Linux users**

Automatically detects your Linux distribution and uses the appropriate package manager:
- **Debian/Ubuntu**: Uses `apt` package manager
- **Red Hat/CentOS/Fedora**: Uses `dnf` or `yum` package manager  
- **Arch Linux**: Uses `pacman` package manager
- **openSUSE**: Uses `zypper` package manager

**Usage:**
```bash
./scripts/build-linux.sh [OPTIONS]
```

#### `build-debian.sh` - Debian/Ubuntu Specific
Optimized for Debian-based distributions (Ubuntu, Linux Mint, Pop!_OS).

**Usage:**
```bash
./scripts/build-debian.sh [OPTIONS]
```

#### `build-redhat.sh` - Red Hat/CentOS/Fedora Specific
Optimized for Red Hat-based distributions with EPEL repository support.

**Usage:**
```bash
./scripts/build-redhat.sh [OPTIONS]
```

### macOS Script

#### `build-macos.sh` - macOS Build Script
Designed for macOS systems with Homebrew and Xcode support.

**Usage:**
```bash
./scripts/build-macos.sh [OPTIONS]
```

### Windows Script

#### `build-windows.bat` - Windows Build Script
Designed for Windows systems with Visual Studio and CMake support.

**Usage:**
```cmd
scripts\build-windows.bat [OPTIONS]
```

### Docker Scripts

#### `build-docker.sh` - Docker Build Script
Cross-platform Docker build automation with multi-architecture support.

**Usage:**
```bash
./scripts/build-docker.sh [OPTIONS]
```

#### `deploy-docker.sh` - Docker Deployment Script
Deploy and manage Docker containers for {{PROJECT_NAME}}.

**Usage:**
```bash
./scripts/deploy-docker.sh [OPTIONS]
```

## Common Options

All scripts support the following options:

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-d, --deps` | Install dependencies only |
| `-b, --build` | Build project only |
| `-t, --test` | Run tests only |
| `-i, --install` | Install project only |
| `-p, --package` | Create package only |
| `-s, --service` | Create system service (Linux) / Windows service |
| `-a, --all` | Full build and install (default) |
| `--static` | Build static binary |
| `--clean` | Clean build directory before building |

## Quick Start

### Linux Users
```bash
# From project root directory
./scripts/build-linux.sh

# Or for specific distribution
./scripts/build-debian.sh      # Debian/Ubuntu
./scripts/build-redhat.sh      # Red Hat/CentOS/Fedora
```

### macOS Users
```bash
# From project root directory
./scripts/build-macos.sh

# Build static binary
./scripts/build-macos.sh --static
```

### Windows Users
```cmd
# From project root directory
scripts\build-windows.bat

# Or for specific operations
scripts\build-windows.bat --deps
scripts\build-windows.bat --build
```

### Docker Users
```bash
# Build Docker image
./scripts/build-docker.sh

# Build multi-architecture image
./scripts/build-docker.sh --platforms linux/amd64,linux/arm64

# Deploy container
./scripts/deploy-docker.sh
```

## What Each Script Does

### 1. **Dependency Installation**
- Detects your system and package manager
- Installs required build tools (gcc, cmake, pkg-config)
- Installs development libraries (OpenSSL, jsoncpp)
- Installs optional development tools (clang-format, cppcheck, valgrind)

### 2. **Project Building**
- Creates and configures build directory
- Runs CMake configuration with optimal settings
- Compiles the project using all available CPU cores
- Handles build errors gracefully
- Supports both regular and static builds

### 3. **Testing**
- Runs the test suite
- Reports test results
- Continues even if some tests fail

### 4. **Installation**
- Installs binaries, libraries, and headers
- Creates configuration directories
- Sets up documentation
- Tests the installed binary

### 5. **Package Creation**
- Creates distribution packages (DEB, RPM, MSI, DMG, PKG)
- Uses CPack when available
- Creates static binary packages
- Lists created packages

### 6. **Service Setup**
- **Linux**: Creates and enables systemd service
- **macOS**: Creates launchd service
- **Windows**: Creates Windows service using sc.exe

### 7. **Docker Support**
- Multi-architecture Docker builds
- Cross-platform containerization
- Container deployment and management

## Prerequisites

### Linux
- `sudo` access for package installation
- Internet connection for package downloads
- At least 2GB free disk space

### macOS
- Xcode Command Line Tools
- Homebrew package manager
- At least 2GB free disk space

### Windows
- Visual Studio 2017 or later with C++ support
- CMake 3.16 or later
- Administrator privileges (recommended)
- Git for vcpkg installation

### Docker
- Docker Engine 20.10 or later
- Docker Buildx for multi-architecture builds
- Internet connection for base images

## Troubleshooting

### Common Issues

#### Permission Denied
```bash
# Make scripts executable
chmod +x scripts/build-*.sh

# Run from project root directory
cd /path/to/{{PROJECT_NAME}}
./scripts/build-linux.sh
```

#### Missing Dependencies
```bash
# Install dependencies only
./scripts/build-linux.sh --deps

# Then build
./scripts/build-linux.sh --build
```

#### Build Failures
```bash
# Clean build directory and retry
rm -rf build
./scripts/build-linux.sh --build
```

#### Installation Issues
```bash
# Check if you have sudo access
sudo -l

# Try installing to user directory
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make install
```

### Getting Help

1. **Check the script help:**
   ```bash
   ./scripts/build-linux.sh --help
   ```

2. **Review the [Troubleshooting Guide](../docs/troubleshooting/README.md)**

3. **Check build logs in the `build/` directory**

4. **Verify your system meets the [requirements](../docs/installation/README.md#prerequisites)**

## Customization

### Environment Variables
You can customize the build process by setting environment variables:

```bash
# Custom install prefix
export CMAKE_INSTALL_PREFIX=/opt/{{PROJECT_NAME}}
./scripts/build-linux.sh

# Custom build type
export CMAKE_BUILD_TYPE=Debug
./scripts/build-linux.sh

# Static build
export ENABLE_STATIC_LINKING=ON
./scripts/build-linux.sh --static
```

### CMake Options
Modify the scripts to add custom CMake options:

```bash
# In build_project() function, add:
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_LOGGING=ON \
    -DENABLE_SSL=ON \
    -DENABLE_STATIC_LINKING=ON \
    -DCUSTOM_OPTION=ON  # Add your custom options here
```

## Contributing

When adding new build scripts or modifying existing ones:

1. **Follow the existing pattern** for consistency
2. **Add proper error handling** and user feedback
3. **Test on target platforms** before committing
4. **Update this README** with new features
5. **Add comments** explaining platform-specific logic

## Platform Support Matrix

| Platform | Script | Package Manager | Service Manager | Docker Support |
|----------|--------|----------------|-----------------|----------------|
| Ubuntu/Debian | `build-debian.sh` | apt | systemd | ✅ |
| Red Hat/CentOS | `build-redhat.sh` | dnf/yum | systemd | ✅ |
| Fedora | `build-redhat.sh` | dnf | systemd | ✅ |
| Arch Linux | `build-linux.sh` | pacman | systemd | ✅ |
| openSUSE | `build-linux.sh` | zypper | systemd | ✅ |
| Generic Linux | `build-linux.sh` | auto-detect | systemd | ✅ |
| macOS | `build-macos.sh` | homebrew | launchd | ✅ |
| Windows | `build-windows.bat` | vcpkg | Windows Service | ✅ |

## Examples

### Development Workflow
```bash
# Install dependencies once
./scripts/build-linux.sh --deps

# Build and test during development
./scripts/build-linux.sh --build --test

# Build static binary
./scripts/build-linux.sh --static

# Install when ready
./scripts/build-linux.sh --install

# Create service for production
./scripts/build-linux.sh --service
```

### CI/CD Integration
```bash
# In your CI pipeline
./scripts/build-linux.sh --build --test --package

# Check exit code
if [ $? -eq 0 ]; then
    echo "Build successful"
else
    echo "Build failed"
    exit 1
fi
```

### Docker Integration
```bash
# Use in Dockerfile
COPY scripts/build-linux.sh /tmp/
RUN chmod +x /tmp/build-linux.sh
RUN /tmp/build-linux.sh --deps --build --install

# Multi-architecture build
./scripts/build-docker.sh --platforms linux/amd64,linux/arm64 --push
```

### Static Binary Distribution
```bash
# Create static binary packages
./scripts/build-linux.sh --static --package

# This creates:
# - {{PROJECT_NAME}}-{{VERSION}}-static-linux.tar.gz
# - {{PROJECT_NAME}}-{{VERSION}}-static-macos.tar.gz
# - {{PROJECT_NAME}}-{{VERSION}}-static-windows.zip
```
