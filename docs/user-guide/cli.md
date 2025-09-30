# Command Line Interface

This guide covers all command line options and usage patterns for Simple NFS Daemon.

## Basic Usage

```bash
simple-nfsd [OPTIONS]
```

## Command Line Options

### Configuration Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-c` | `--config` | Configuration file path | `/etc/simple-nfsd/simple-nfsd.conf` |
| `-t` | `--test-config` | Test configuration and exit | - |
| `-v` | `--validate` | Validate configuration syntax | - |

### Daemon Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-d` | `--daemon` | Run as daemon | - |
| `-p` | `--pid-file` | PID file path | `/var/run/simple-nfsd.pid` |
| `-f` | `--foreground` | Run in foreground | - |

### Logging Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-l` | `--log-file` | Log file path | `/var/log/simple-nfsd.log` |
| `-v` | `--verbose` | Verbose logging | - |
| `-q` | `--quiet` | Quiet mode | - |

### Information Options

| Option | Long Option | Description | Default |
|--------|-------------|-------------|---------|
| `-h` | `--help` | Show help message | - |
| `-V` | `--version` | Show version information | - |
| `-s` | `--show-config` | Show current configuration | - |
| `-e` | `--show-exports` | Show active exports | - |
| `-S` | `--stats` | Show statistics | - |
| `-C` | `--show-connections` | Show active connections | - |

## Usage Examples

### Basic Server Start

```bash
# Start with default configuration
sudo simple-nfsd

# Start with custom configuration
sudo simple-nfsd -c /etc/simple-nfsd/custom.conf

# Start in foreground with verbose logging
sudo simple-nfsd -f -v
```

### Daemon Mode

```bash
# Start as daemon
sudo simple-nfsd -d

# Start as daemon with custom PID file
sudo simple-nfsd -d -p /var/run/custom-nfsd.pid

# Start as daemon with custom log file
sudo simple-nfsd -d -l /var/log/custom-nfsd.log
```

### Configuration Testing

```bash
# Test configuration syntax
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --validate

# Show parsed configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --show-config
```

### Information and Monitoring

```bash
# Show version information
simple-nfsd --version

# Show help
simple-nfsd --help

# Show active exports
sudo simple-nfsd --show-exports

# Show statistics
sudo simple-nfsd --stats

# Show active connections
sudo simple-nfsd --show-connections
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `NFSD_CONFIG_FILE` | Configuration file path | `/etc/simple-nfsd/simple-nfsd.conf` |
| `NFSD_LOG_LEVEL` | Log level | `info` |
| `NFSD_LOG_FILE` | Log file path | `/var/log/simple-nfsd.log` |
| `NFSD_PID_FILE` | PID file path | `/var/run/simple-nfsd.pid` |

### Using Environment Variables

```bash
# Set configuration via environment
export NFSD_CONFIG_FILE=/etc/simple-nfsd/production.conf
export NFSD_LOG_LEVEL=debug
sudo simple-nfsd

# Or use inline
NFSD_LOG_LEVEL=debug sudo simple-nfsd -f
```

## Signal Handling

Simple NFS Daemon responds to the following signals:

| Signal | Action | Description |
|--------|--------|-------------|
| `SIGTERM` | Graceful shutdown | Stop accepting new requests, finish current requests |
| `SIGINT` | Graceful shutdown | Same as SIGTERM |
| `SIGHUP` | Reload configuration | Reload configuration without restart |
| `SIGUSR1` | Rotate logs | Rotate log files |
| `SIGUSR2` | Show statistics | Display current statistics |

### Signal Examples

```bash
# Graceful shutdown
sudo kill -TERM $(pidof simple-nfsd)

# Reload configuration
sudo kill -HUP $(pidof simple-nfsd)

# Rotate logs
sudo kill -USR1 $(pidof simple-nfsd)

# Show statistics
sudo kill -USR2 $(pidof simple-nfsd)
```

## Systemd Integration

### Service Commands

```bash
# Start service
sudo systemctl start simple-nfsd

# Stop service
sudo systemctl stop simple-nfsd

# Restart service
sudo systemctl restart simple-nfsd

# Reload configuration
sudo systemctl reload simple-nfsd

# Enable auto-start
sudo systemctl enable simple-nfsd

# Disable auto-start
sudo systemctl disable simple-nfsd

# Check status
sudo systemctl status simple-nfsd
```

### Service Configuration

The systemd service file is typically located at `/lib/systemd/system/simple-nfsd.service`:

```ini
[Unit]
Description=Simple NFS Daemon
After=network.target

[Service]
Type=notify
ExecStart=/usr/bin/simple-nfsd -d
ExecReload=/bin/kill -HUP $MAINPID
PIDFile=/var/run/simple-nfsd.pid
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

## Docker Usage

### Basic Docker Run

```bash
# Run with default configuration
docker run -d --name simple-nfsd --net host simpledaemons/simple-nfsd:latest

# Run with custom configuration
docker run -d --name simple-nfsd --net host \
  -v /etc/simple-nfsd:/etc/simple-nfsd \
  -v /var/lib/simple-nfsd/shares:/var/lib/simple-nfsd/shares \
  simpledaemons/simple-nfsd:latest

# Run with environment variables
docker run -d --name simple-nfsd --net host \
  -e NFSD_LOG_LEVEL=debug \
  -e NFSD_CONFIG_FILE=/etc/simple-nfsd/debug.conf \
  simpledaemons/simple-nfsd:latest
```

### Docker Compose

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
    environment:
      - NFSD_LOG_LEVEL=info
    restart: unless-stopped
```

## Troubleshooting

### Common Issues

#### Permission Denied

```bash
# Check file permissions
ls -la /etc/simple-nfsd/
sudo chown -R nfsd:nfsd /etc/simple-nfsd/

# Check if running as root
sudo simple-nfsd -f
```

#### Port Already in Use

```bash
# Check what's using port 2049
sudo netstat -tulpn | grep :2049
sudo lsof -i :2049

# Stop conflicting service
sudo systemctl stop nfs-server
```

#### Configuration Errors

```bash
# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Check JSON syntax
python -m json.tool /etc/simple-nfsd/simple-nfsd.conf
```

### Debug Mode

```bash
# Run with debug logging
sudo simple-nfsd -f -v

# Run with debug configuration
sudo simple-nfsd -c /etc/simple-nfsd/debug.conf -f -v

# Check system logs
sudo journalctl -u simple-nfsd -f
```

### Performance Monitoring

```bash
# Show statistics
sudo simple-nfsd --stats

# Show active exports
sudo simple-nfsd --show-exports

# Show active connections
sudo simple-nfsd --show-connections

# Monitor in real-time
watch -n 1 'sudo simple-nfsd --stats'
```

## Advanced Usage

### Custom Configuration Directory

```bash
# Use custom configuration directory
sudo simple-nfsd -c /opt/simple-nfsd/config/production.conf
```

### Multiple Instances

```bash
# Run multiple instances on different ports
sudo simple-nfsd -c /etc/simple-nfsd/instance1.conf -p /var/run/simple-nfsd-1.pid
sudo simple-nfsd -c /etc/simple-nfsd/instance2.conf -p /var/run/simple-nfsd-2.pid
```

### Log Rotation

```bash
# Rotate logs manually
sudo kill -USR1 $(pidof simple-nfsd)

# Configure logrotate
sudo vim /etc/logrotate.d/simple-nfsd
```

## Client Commands

### NFS Client Setup

```bash
# Install NFS client tools
sudo apt install nfs-common  # Ubuntu/Debian
sudo yum install nfs-utils   # CentOS/RHEL

# List available exports
showmount -e localhost

# Mount NFS share
sudo mount -t nfs localhost:/var/lib/simple-nfsd/shares /mnt/nfs

# Mount with specific options
sudo mount -t nfs -o rw,sync,hard,intr localhost:/var/lib/simple-nfsd/shares /mnt/nfs

# Unmount
sudo umount /mnt/nfs
```

### Auto-mount Configuration

```bash
# Add to /etc/fstab
echo "localhost:/var/lib/simple-nfsd/shares /mnt/nfs nfs rw,sync,hard,intr 0 0" | sudo tee -a /etc/fstab

# Test mount
sudo mount -a
```

## Best Practices

1. **Always test configuration**: Use `--test-config` before starting
2. **Use daemon mode in production**: Use `-d` flag for production deployments
3. **Set appropriate log levels**: Use `info` for production, `debug` for troubleshooting
4. **Use PID files**: Specify PID file for process management
5. **Monitor performance**: Use `--stats` to monitor server performance
6. **Backup configurations**: Keep configuration backups
7. **Use systemd**: Use systemd for service management in production

## Next Steps

- [Service Management](service.md)
- [Monitoring](monitoring.md)
- [Configuration Guide](../configuration/README.md)
- [Troubleshooting Guide](../troubleshooting/README.md)
