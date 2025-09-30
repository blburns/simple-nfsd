# Quick Start Guide

Get Simple NFS Daemon up and running in minutes with this quick start guide.

## Prerequisites

- Simple NFS Daemon installed (see [Installation Guide](installation.md))
- Root or sudo access
- Basic understanding of NFS concepts

## Step 1: Create Basic Configuration

Create a minimal configuration file:

```bash
sudo mkdir -p /etc/simple-nfsd
sudo tee /etc/simple-nfsd/simple-nfsd.conf > /dev/null << 'EOF'
{
  "nfs": {
    "listen": ["0.0.0.0:2049"],
    "exports": [
      {
        "path": "/var/lib/simple-nfsd/shares",
        "clients": ["*"],
        "options": ["rw", "sync", "no_subtree_check"]
      }
    ],
    "logging": {
      "enable": true,
      "level": "info"
    }
  }
}
EOF
```

## Step 2: Create Share Directory

```bash
sudo mkdir -p /var/lib/simple-nfsd/shares
sudo chown nfsd:nfsd /var/lib/simple-nfsd/shares
sudo chmod 755 /var/lib/simple-nfsd/shares
```

## Step 3: Test Configuration

```bash
# Test configuration syntax
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config
```

## Step 4: Start the Server

### Option A: Foreground Mode (for testing)

```bash
sudo simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf -f -v
```

### Option B: Daemon Mode (for production)

```bash
sudo simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf -d
```

### Option C: Systemd Service

```bash
sudo systemctl start simple-nfsd
sudo systemctl enable simple-nfsd
```

## Step 5: Test NFS Mount

In another terminal, test the NFS mount:

```bash
# Create mount point
sudo mkdir -p /mnt/nfs-test

# Mount the NFS share
sudo mount -t nfs localhost:/var/lib/simple-nfsd/shares /mnt/nfs-test

# Test write access
echo "Hello NFS!" | sudo tee /mnt/nfs-test/test.txt

# Test read access
sudo cat /mnt/nfs-test/test.txt

# Unmount
sudo umount /mnt/nfs-test
```

## Step 6: Verify Service Status

```bash
# Check if service is running
sudo systemctl status simple-nfsd

# Check logs
sudo journalctl -u simple-nfsd -f

# Check active exports
sudo simple-nfsd --show-exports
```

## Basic Configuration Examples

### Home Network Setup

```json
{
  "nfs": {
    "listen": ["0.0.0.0:2049"],
    "exports": [
      {
        "path": "/home/shared",
        "clients": ["192.168.1.0/24"],
        "options": ["rw", "sync", "no_subtree_check", "no_root_squash"]
      }
    ],
    "logging": {
      "enable": true,
      "level": "info"
    }
  }
}
```

### Office Network Setup

```json
{
  "nfs": {
    "listen": ["0.0.0.0:2049"],
    "exports": [
      {
        "path": "/var/shared/documents",
        "clients": ["10.0.0.0/8"],
        "options": ["rw", "sync", "no_subtree_check"]
      },
      {
        "path": "/var/shared/backups",
        "clients": ["10.0.1.0/24"],
        "options": ["ro", "sync", "no_subtree_check"]
      }
    ],
    "logging": {
      "enable": true,
      "level": "info",
      "log_file": "/var/log/simple-nfsd.log"
    }
  }
}
```

### Secure Setup

```json
{
  "nfs": {
    "listen": ["0.0.0.0:2049"],
    "exports": [
      {
        "path": "/var/secure/data",
        "clients": ["192.168.1.100", "192.168.1.101"],
        "options": ["rw", "sync", "no_subtree_check", "root_squash"]
      }
    ],
    "security": {
      "authentication": "sys",
      "encryption": false
    },
    "logging": {
      "enable": true,
      "level": "info",
      "log_file": "/var/log/simple-nfsd.log"
    }
  }
}
```

## Common Commands

### Service Management

```bash
# Start service
sudo systemctl start simple-nfsd

# Stop service
sudo systemctl stop simple-nfsd

# Restart service
sudo systemctl restart simple-nfsd

# Reload configuration
sudo systemctl reload simple-nfsd

# Check status
sudo systemctl status simple-nfsd
```

### Monitoring

```bash
# Show active exports
sudo simple-nfsd --show-exports

# Show statistics
sudo simple-nfsd --stats

# Show active connections
sudo simple-nfsd --show-connections
```

### Client Commands

```bash
# List available exports
showmount -e localhost

# Mount NFS share
sudo mount -t nfs localhost:/var/lib/simple-nfsd/shares /mnt/nfs

# Mount with specific options
sudo mount -t nfs -o rw,sync,hard,intr localhost:/var/lib/simple-nfsd/shares /mnt/nfs

# Unmount
sudo umount /mnt/nfs
```

## Troubleshooting

### Common Issues

**Permission Denied:**
```bash
# Check file permissions
ls -la /var/lib/simple-nfsd/shares
sudo chown -R nfsd:nfsd /var/lib/simple-nfsd/shares
```

**Mount Failed:**
```bash
# Check if NFS service is running
sudo systemctl status simple-nfsd

# Check exports
sudo simple-nfsd --show-exports

# Check firewall
sudo ufw status
sudo firewall-cmd --list-all
```

**Connection Refused:**
```bash
# Check if port 2049 is listening
sudo netstat -tulpn | grep :2049

# Check logs
sudo journalctl -u simple-nfsd -f
```

## Next Steps

Now that you have Simple NFS Daemon running:

1. **Configure Security**: Set up authentication and access controls
2. **Add More Exports**: Configure additional shared directories
3. **Set Up Monitoring**: Configure logging and monitoring
4. **Production Deployment**: Follow the [Production Deployment Guide](../deployment/production.md)

## Additional Resources

- [Configuration Guide](../configuration/README.md) - Complete configuration reference
- [Command Line Interface](../user-guide/cli.md) - All available commands
- [Troubleshooting Guide](../troubleshooting/README.md) - Common issues and solutions
- [Security Configuration](../config/SECURITY_CONFIG.md) - Security best practices
