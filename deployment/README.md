# simple-nfsd Deployment

This directory contains deployment configurations and examples for simple-nfsd.

## Directory Structure

```
deployment/
├── systemd/                    # Linux systemd service files
│   └── simple-nfsd.service
├── launchd/                    # macOS launchd service files
│   └── com.simple-nfsd.simple-nfsd.plist
├── logrotate.d/                # Linux log rotation configuration
│   └── simple-nfsd
├── windows/                    # Windows service management
│   └── simple-nfsd.service.bat
└── examples/                   # Deployment examples
    └── docker/                 # Docker deployment examples
        ├── docker-compose.yml
        └── README.md
```

## Platform-Specific Deployment

### Linux (systemd)

1. **Install the service file:**
   ```bash
   sudo cp deployment/systemd/simple-nfsd.service /etc/systemd/system/
   sudo systemctl daemon-reload
   ```

2. **Create user and group:**
   ```bash
   sudo useradd --system --no-create-home --shell /bin/false simple-nfsd
   ```

3. **Enable and start the service:**
   ```bash
   sudo systemctl enable simple-nfsd
   sudo systemctl start simple-nfsd
   ```

4. **Check status:**
   ```bash
   sudo systemctl status simple-nfsd
   sudo journalctl -u simple-nfsd -f
   ```

### macOS (launchd)

1. **Install the plist file:**
   ```bash
   sudo cp deployment/launchd/com.simple-nfsd.simple-nfsd.plist /Library/LaunchDaemons/
   sudo chown root:wheel /Library/LaunchDaemons/com.simple-nfsd.simple-nfsd.plist
   ```

2. **Load and start the service:**
   ```bash
   sudo launchctl load /Library/LaunchDaemons/com.simple-nfsd.simple-nfsd.plist
   sudo launchctl start com.simple-nfsd.simple-nfsd
   ```

3. **Check status:**
   ```bash
   sudo launchctl list | grep simple-nfsd
   tail -f /var/log/simple-nfsd.log
   ```

### Windows

1. **Run as Administrator:**
   ```cmd
   # Install service
   deployment\windows\simple-nfsd.service.bat install
   
   # Start service
   deployment\windows\simple-nfsd.service.bat start
   
   # Check status
   deployment\windows\simple-nfsd.service.bat status
   ```

2. **Service management:**
   ```cmd
   # Stop service
   deployment\windows\simple-nfsd.service.bat stop
   
   # Restart service
   deployment\windows\simple-nfsd.service.bat restart
   
   # Uninstall service
   deployment\windows\simple-nfsd.service.bat uninstall
   ```

## Log Rotation (Linux)

1. **Install logrotate configuration:**
   ```bash
   sudo cp deployment/logrotate.d/simple-nfsd /etc/logrotate.d/
   ```

2. **Test logrotate configuration:**
   ```bash
   sudo logrotate -d /etc/logrotate.d/simple-nfsd
   ```

3. **Force log rotation:**
   ```bash
   sudo logrotate -f /etc/logrotate.d/simple-nfsd
   ```

## Docker Deployment

See [examples/docker/README.md](examples/docker/README.md) for detailed Docker deployment instructions.

### Quick Start

```bash
# Build and run with Docker Compose
cd deployment/examples/docker
docker-compose up -d

# Check status
docker-compose ps
docker-compose logs simple-nfsd
```

## Configuration

### Service Configuration

Each platform has specific configuration requirements:

- **Linux**: Edit `/etc/systemd/system/simple-nfsd.service`
- **macOS**: Edit `/Library/LaunchDaemons/com.simple-nfsd.simple-nfsd.plist`
- **Windows**: Edit the service binary path in the batch file

### Application Configuration

Place your application configuration in:
- **Linux/macOS**: `/etc/simple-nfsd/simple-nfsd.conf`
- **Windows**: `%PROGRAMFILES%\simple-nfsd\simple-nfsd.conf`

## Security Considerations

### User and Permissions

1. **Create dedicated user:**
   ```bash
   # Linux
   sudo useradd --system --no-create-home --shell /bin/false simple-nfsd
   
   # macOS
   sudo dscl . -create /Users/_simple-nfsd UserShell /usr/bin/false
   sudo dscl . -create /Users/_simple-nfsd UniqueID 200
   sudo dscl . -create /Users/_simple-nfsd PrimaryGroupID 200
   sudo dscl . -create /Groups/_simple-nfsd GroupID 200
   ```

2. **Set proper permissions:**
   ```bash
   # Configuration files
   sudo chown root:simple-nfsd /etc/simple-nfsd/simple-nfsd.conf
   sudo chmod 640 /etc/simple-nfsd/simple-nfsd.conf
   
   # Log files
   sudo chown simple-nfsd:simple-nfsd /var/log/simple-nfsd/
   sudo chmod 755 /var/log/simple-nfsd/
   ```

### Firewall Configuration

Configure firewall rules as needed:

```bash
# Linux (ufw)
sudo ufw allow 80/tcp

# Linux (firewalld)
sudo firewall-cmd --permanent --add-port=80/tcp
sudo firewall-cmd --reload

# macOS
sudo pfctl -f /etc/pf.conf
```

## Monitoring

### Health Checks

1. **Service status:**
   ```bash
   # Linux
   sudo systemctl is-active simple-nfsd
   
   # macOS
   sudo launchctl list | grep simple-nfsd
   
   # Windows
   sc query simple-nfsd
   ```

2. **Port availability:**
   ```bash
   netstat -tlnp | grep 80
   ss -tlnp | grep 80
   ```

3. **Process monitoring:**
   ```bash
   ps aux | grep simple-nfsd
   top -p $(pgrep simple-nfsd)
   ```

### Log Monitoring

1. **Real-time logs:**
   ```bash
   # Linux
   sudo journalctl -u simple-nfsd -f
   
   # macOS
   tail -f /var/log/simple-nfsd.log
   
   # Windows
   # Use Event Viewer or PowerShell Get-WinEvent
   ```

2. **Log analysis:**
   ```bash
   # Search for errors
   sudo journalctl -u simple-nfsd --since "1 hour ago" | grep -i error
   
   # Count log entries
   sudo journalctl -u simple-nfsd --since "1 day ago" | wc -l
   ```

## Troubleshooting

### Common Issues

1. **Service won't start:**
   - Check configuration file syntax
   - Verify user permissions
   - Check port availability
   - Review service logs

2. **Permission denied:**
   - Ensure service user exists
   - Check file permissions
   - Verify directory ownership

3. **Port already in use:**
   - Check what's using the port: `netstat -tlnp | grep 80`
   - Stop conflicting service or change port

4. **Service stops unexpectedly:**
   - Check application logs
   - Verify resource limits
   - Review system logs

### Debug Mode

Run the service in debug mode for troubleshooting:

```bash
# Linux/macOS
sudo -u simple-nfsd /usr/local/bin/simple-nfsd --debug

# Windows
simple-nfsd.exe --debug
```

### Log Levels

Adjust log level for more verbose output:

```bash
# Set log level in configuration
log_level = debug

# Or via environment variable
export SIMPLE-NFSD_LOG_LEVEL=debug
```

## Backup and Recovery

### Configuration Backup

```bash
# Backup configuration
sudo tar -czf simple-nfsd-config-backup-$(date +%Y%m%d).tar.gz /etc/simple-nfsd/

# Backup logs
sudo tar -czf simple-nfsd-logs-backup-$(date +%Y%m%d).tar.gz /var/log/simple-nfsd/
```

### Service Recovery

```bash
# Stop service
sudo systemctl stop simple-nfsd

# Restore configuration
sudo tar -xzf simple-nfsd-config-backup-YYYYMMDD.tar.gz -C /

# Start service
sudo systemctl start simple-nfsd
```

## Updates

### Service Update Process

1. **Stop service:**
   ```bash
   sudo systemctl stop simple-nfsd
   ```

2. **Backup current version:**
   ```bash
   sudo cp /usr/local/bin/simple-nfsd /usr/local/bin/simple-nfsd.backup
   ```

3. **Install new version:**
   ```bash
   sudo cp simple-nfsd /usr/local/bin/
   sudo chmod +x /usr/local/bin/simple-nfsd
   ```

4. **Start service:**
   ```bash
   sudo systemctl start simple-nfsd
   ```

5. **Verify update:**
   ```bash
   sudo systemctl status simple-nfsd
   simple-nfsd --version
   ```
