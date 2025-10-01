# Simple NFS Daemon

[![Version](https://img.shields.io/badge/version-0.2.0-blue.svg)](https://github.com/blburns/simple-nfsd)
[![License](https://img.shields.io/badge/license-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/blburns/simple-nfsd)

A lightweight, high-performance NFS server implementation designed for modern systems with support for NFSv2, NFSv3, and NFSv4 protocols.

**Current Version**: 0.2.1 (NFSv3 Complete - Enhanced Protocol Support with Portmapper)

## Features

- **Multi-Protocol Support**: NFSv2, NFSv3, and NFSv4 compatibility
- **RPC Portmapper Integration**: Full RPC service registration and discovery
- **Multi-Format Configuration**: Support for INI, JSON, and YAML configuration files
- **Cross-Platform**: Linux, macOS, and Windows support
- **High Performance**: Optimized for high-throughput file sharing
- **Security**: Modern authentication and access control
- **Easy Deployment**: Docker containerization and system service support
- **Comprehensive Testing**: Unit tests with GTest framework
- **Static Linking**: Self-contained binary generation

## Quick Start

### Building

```bash
# Regular build
make build

# Static binary build
make static-build

# Run tests
make test

# Create packages
make package
make static-package
```

### Running

```bash
# Run in foreground
./simple-nfsd

# Run as daemon
./simple-nfsd --daemon

# With custom configuration
./simple-nfsd --config /path/to/config.json

# Show help
./simple-nfsd --help
```

## Configuration

Simple NFS Daemon supports multiple configuration formats:

### INI Format (Default)
```ini
[global]
server_name = "Simple NFS Daemon"
listen_address = "0.0.0.0"
listen_port = 2049
max_connections = 1000
log_level = "info"
```

### JSON Format
```json
{
  "global": {
    "server_name": "Simple NFS Daemon",
    "listen_address": "0.0.0.0",
    "listen_port": 2049,
    "max_connections": 1000,
    "log_level": "info"
  }
}
```

### YAML Format
```yaml
global:
  server_name: "Simple NFS Daemon"
  listen_address: "0.0.0.0"
  listen_port: 2049
  max_connections: 1000
  log_level: "info"
```

Configuration files are automatically detected based on file extension (.conf, .json, .yaml/.yml).

## Configuration Options

### Global Settings
- `server_name`: Server identification string
- `listen_address`: IP address to bind to (default: 0.0.0.0)
- `listen_port`: Port to listen on (default: 2049)
- `max_connections`: Maximum concurrent connections
- `thread_count`: Number of worker threads
- `security_mode`: Authentication mode (user, system)
- `root_squash`: Enable root squashing
- `log_level`: Logging level (debug, info, warn, error)

### Export Settings
- `path`: Directory path to export
- `clients`: Allowed client addresses/networks
- `options`: NFS export options (rw, ro, sync, etc.)
- `comment`: Export description

## Service Management

### Systemd (Linux)
```bash
# Install service
sudo make service-install

# Start service
sudo systemctl start simple-nfsd

# Enable auto-start
sudo systemctl enable simple-nfsd

# Check status
sudo systemctl status simple-nfsd
```

### Launchd (macOS)
```bash
# Install service
sudo make service-install

# Load service
sudo launchctl load /Library/LaunchDaemons/com.simple-nfsd.simple-nfsd.plist

# Check status
sudo launchctl list | grep simple-nfsd
```

### Windows Service
```cmd
# Install service
simple-nfsd --install-service

# Start service
net start simple-nfsd

# Stop service
net stop simple-nfsd
```

## Docker Support

### Using Docker Compose
```yaml
version: '3.8'
services:
  simple-nfsd:
    image: simple-nfsd:latest
    ports:
      - "2049:2049"
    volumes:
      - ./exports:/var/lib/simple-nfsd/exports
      - ./config:/etc/simple-nfsd
    environment:
      - NFSD_CONFIG_FILE=/etc/simple-nfsd/simple-nfsd.json
```

### Building Docker Image
```bash
# Build image
docker build -t simple-nfsd .

# Run container
docker run -d \
  --name simple-nfsd \
  -p 2049:2049 \
  -v /path/to/exports:/var/lib/simple-nfsd/exports \
  simple-nfsd
```

## Development

### Prerequisites
- CMake 3.16+
- C++17 compatible compiler
- OpenSSL (for encryption)
- JsonCPP (for JSON support)
- yaml-cpp (for YAML support, optional)
- GTest (for testing)

### Building from Source
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
```

### Testing
```bash
# Run all tests
make test

# Run specific test
./build/simple-nfsd-tests --gtest_filter=NfsdAppTest.*

# Run with verbose output
make test-verbose
```

## Package Generation

The build system supports creating packages for multiple platforms:

### Linux
- DEB packages (Ubuntu/Debian)
- RPM packages (Red Hat/CentOS/Fedora)

### macOS
- DMG disk images
- PKG installer packages

### Windows
- MSI installer packages
- ZIP archives

### Source Packages
- TAR.GZ archives
- ZIP archives

## Performance Tuning

### Recommended Settings
```ini
[global]
# Increase for high-load environments
max_connections = 5000
thread_count = 16

# Optimize I/O
read_size = 65536
write_size = 65536

# Enable caching
cache_enabled = true
cache_size = "500MB"
cache_ttl = 7200
```

## Security Considerations

- Use `root_squash` to prevent root access from clients
- Configure appropriate client access controls
- Enable logging for audit trails
- Use secure authentication methods
- Regularly update to latest version

## Troubleshooting

### Common Issues

**Port already in use**
```bash
# Check what's using port 2049
sudo netstat -tlnp | grep 2049
sudo lsof -i :2049
```

**Permission denied**
```bash
# Check file permissions
ls -la /var/lib/simple-nfsd/exports/
sudo chown -R nfsd:nfsd /var/lib/simple-nfsd/
```

**Configuration errors**
```bash
# Validate configuration
./simple-nfsd --config /path/to/config.json --validate
```

### Logs
- System logs: `/var/log/simple-nfsd/simple-nfsd.log`
- Systemd logs: `journalctl -u simple-nfsd`
- Launchd logs: `log show --predicate 'process == "simple-nfsd"'`

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

### Code Style
- Follow existing code style
- Use meaningful variable names
- Add comments for complex logic
- Write unit tests for new features

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## Support

- **Documentation**: [ROADMAP.md](ROADMAP.md)
- **Issues**: [GitHub Issues](https://github.com/SimpleDaemons/simple-nfsd/issues)
- **Email**: contact@simpledaemons.org
- **Website**: https://simpledaemons.org

## Roadmap

See [ROADMAP.md](ROADMAP.md) for detailed development plans and upcoming features.

Current development focuses on:
- Phase 1: Foundation (✅ Complete)
- Phase 2: Core NFS Protocol Implementation (🔄 In Progress)
  - NFSv2 Protocol (✅ 100% Complete - v0.2.0)
  - NFSv3 Protocol (✅ 100% Complete - v0.2.1)
  - NFSv4 Protocol (📋 Planned - v0.2.2)
- Phase 3: File System Operations (📋 Planned)
- Phase 4: Advanced Features (Planned)
- Phase 5: Enterprise Features (Planned)