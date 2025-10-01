# Installation Guide

This guide covers installing Simple NFS Daemon on various platforms.

## System Requirements

### Minimum Requirements
- **CPU**: 1 core, 1 GHz
- **RAM**: 512 MB
- **Disk**: 100 MB free space
- **OS**: Linux (kernel 3.10+), macOS 10.15+, Windows 10/11

### Recommended Requirements
- **CPU**: 2+ cores, 2+ GHz
- **RAM**: 2+ GB
- **Disk**: 1+ GB free space
- **Network**: Gigabit Ethernet

## Installation Methods

### From Source

#### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential cmake git libssl-dev libjsoncpp-dev libyaml-cpp-dev
```

**CentOS/RHEL:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake git openssl-devel jsoncpp-devel yaml-cpp-devel
```

**macOS:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install dependencies via Homebrew
brew install cmake openssl jsoncpp yaml-cpp
```

**Windows:**
- Install Visual Studio 2019 or later
- Install CMake
- Install vcpkg for dependencies

#### Build and Install

```bash
# Clone repository
git clone https://github.com/SimpleDaemons/simple-nfsd.git
cd simple-nfsd

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests
make test

# Install
sudo make install
```

### Package Installation

#### Ubuntu/Debian (Coming Soon)

```bash
# Add repository
wget -qO - https://packages.simpledaemons.org/gpg.key | sudo apt-key add -
echo "deb https://packages.simpledaemons.org/ubuntu focal main" | sudo tee /etc/apt/sources.list.d/simpledaemons.list

# Install
sudo apt update
sudo apt install simple-nfsd
```

#### CentOS/RHEL (Coming Soon)

```bash
# Add repository
sudo yum-config-manager --add-repo https://packages.simpledaemons.org/rhel/simpledaemons.repo

# Install
sudo yum install simple-nfsd
```

#### macOS (Coming Soon)

```bash
# Add tap
brew tap simpledaemons/simple-nfsd

# Install
brew install simple-nfsd
```

### Docker Installation

#### Using Docker Hub

```bash
# Pull image
docker pull simpledaemons/simple-nfsd:latest

# Run container
docker run -d \
  --name simple-nfsd \
  --net host \
  -v /etc/simple-nfsd:/etc/simple-nfsd \
  -v /var/lib/simple-nfsd/shares:/var/lib/simple-nfsd/shares \
  simpledaemons/simple-nfsd:latest
```

#### Using Docker Compose

```yaml
version: '3.8'
services:
  simple-nfsd:
    image: simpledaemons/simple-nfsd:latest
    network_mode: host
    volumes:
      - ./config:/etc/simple-nfsd
      - ./shares:/var/lib/simple-nfsd/shares
      - ./logs:/var/log/simple-nfsd
    restart: unless-stopped
    environment:
      - NFSD_CONFIG_FILE=/etc/simple-nfsd/simple-nfsd.conf
```

## Post-Installation

### Create Configuration Directory

```bash
sudo mkdir -p /etc/simple-nfsd
sudo chown nfsd:nfsd /etc/simple-nfsd
```

### Create Log Directory

```bash
sudo mkdir -p /var/log/simple-nfsd
sudo chown nfsd:nfsd /var/log/simple-nfsd
```

### Create Share Directory

```bash
sudo mkdir -p /var/lib/simple-nfsd/shares
sudo chown nfsd:nfsd /var/lib/simple-nfsd/shares
```

### Create System User

```bash
# Create nfsd user
sudo useradd -r -s /bin/false -d /var/lib/simple-nfsd nfsd
```

## Verification

### Check Installation

```bash
# Check version
simple-nfsd --version
# Expected output: Simple NFS Daemon v0.2.1

# Check help
simple-nfsd --help

# Test configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config
```

### Basic Test

```bash
# Start server in foreground
sudo simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf -v

# In another terminal, test NFS mount
sudo mount -t nfs localhost:/var/lib/simple-nfsd/shares /mnt/nfs-test
```

## Uninstallation

### Remove Package

**Ubuntu/Debian:**
```bash
sudo apt remove simple-nfsd
sudo apt autoremove
```

**CentOS/RHEL:**
```bash
sudo yum remove simple-nfsd
```

**macOS:**
```bash
brew uninstall simple-nfsd
```

### Remove from Source

```bash
# From build directory
sudo make uninstall

# Remove configuration files
sudo rm -rf /etc/simple-nfsd
sudo rm -rf /var/lib/simple-nfsd
sudo rm -rf /var/log/simple-nfsd

# Remove user
sudo userdel nfsd
```

## Troubleshooting

### Common Issues

**Permission Denied:**
```bash
# Check file permissions
ls -la /etc/simple-nfsd/
sudo chown -R nfsd:nfsd /etc/simple-nfsd/
```

**Port Already in Use:**
```bash
# Check what's using port 2049
sudo netstat -tulpn | grep :2049
sudo lsof -i :2049
```

**Configuration Errors:**
```bash
# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config
```

### Getting Help

- Check the [Troubleshooting Guide](../troubleshooting/README.md)
- View logs: `sudo journalctl -u simple-nfsd -f`
- Open an issue on [GitHub](https://github.com/SimpleDaemons/simple-nfsd/issues)

## Next Steps

After installation, proceed to:
- [Quick Start Guide](quick-start.md)
- [Configuration Guide](../configuration/README.md)
- [First Steps](first-steps.md)
