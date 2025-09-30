# Configuration Guide

This guide covers all configuration options available in Simple NFS Daemon.

## Configuration File Format

Simple NFS Daemon supports multiple configuration formats:
- **JSON**: Primary format (recommended)
- **YAML**: Alternative format
- **INI**: Legacy format (deprecated)

The configuration file is typically located at `/etc/simple-nfsd/simple-nfsd.conf`.

## Basic Configuration Structure

### JSON Format

```json
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
```

### YAML Format

```yaml
nfs:
  listen:
    - "0.0.0.0:2049"
  exports:
    - path: "/var/lib/simple-nfsd/shares"
      clients: ["*"]
      options: ["rw", "sync", "no_subtree_check"]
  logging:
    enable: true
    level: "info"
```

## Configuration Sections

### Global Settings

#### `listen`
Array of addresses and ports to listen on.

```json
"listen": [
  "0.0.0.0:2049",      // IPv4 on all interfaces
  "[::]:2049",         // IPv6 on all interfaces
  "192.168.1.1:2049"   // Specific interface
]
```

#### `exports`
Array of NFS export configurations.

#### `protocols`
Supported NFS protocol versions.

```json
"protocols": {
  "nfsv2": true,
  "nfsv3": true,
  "nfsv4": true
}
```

### Export Configuration

#### Basic Export Settings

```json
{
  "path": "/var/lib/simple-nfsd/shares",  // Directory to export
  "clients": ["*"],                       // Allowed clients
  "options": ["rw", "sync", "no_subtree_check"]  // Export options
}
```

#### Client Access Control

```json
"clients": [
  "*",                    // All clients
  "192.168.1.0/24",       // Network range
  "192.168.1.100",        // Specific IP
  "*.example.com",        // Hostname pattern
  "192.168.1.100/32"      // Specific IP with mask
]
```

#### Export Options

```json
"options": [
  "rw",                   // Read-write access
  "ro",                   // Read-only access
  "sync",                 // Synchronous writes
  "async",                // Asynchronous writes
  "no_subtree_check",     // Disable subtree checking
  "subtree_check",        // Enable subtree checking
  "no_root_squash",       // Don't squash root user
  "root_squash",          // Squash root user to nobody
  "all_squash",           // Squash all users to nobody
  "no_all_squash",        // Don't squash all users
  "anonuid=65534",        // Anonymous user ID
  "anongid=65534",        // Anonymous group ID
  "secure",               // Require secure port
  "insecure",             // Allow insecure port
  "wdelay",               // Delay writes
  "no_wdelay",            // Don't delay writes
  "hide",                 // Hide subdirectories
  "no_hide",              // Don't hide subdirectories
  "crossmnt",             // Allow cross-mounts
  "no_crossmnt",          // Don't allow cross-mounts
  "nohide",               // Don't hide subdirectories
  "no_subtree_check"      // Disable subtree checking
]
```

#### Advanced Export Settings

```json
{
  "path": "/var/lib/simple-nfsd/shares",
  "clients": ["192.168.1.0/24"],
  "options": ["rw", "sync", "no_subtree_check"],
  "security": {
    "authentication": "sys",
    "encryption": false
  },
  "quotas": {
    "enabled": true,
    "soft_limit": "1GB",
    "hard_limit": "2GB"
  },
  "caching": {
    "enabled": true,
    "ttl": 300
  }
}
```

### Security Configuration

#### Authentication

```json
"security": {
  "authentication": "sys",  // "sys", "krb5", "krb5i", "krb5p"
  "encryption": false,      // Enable encryption
  "integrity": false,       // Enable integrity checking
  "privacy": false          // Enable privacy protection
}
```

#### Access Control

```json
"security": {
  "access_control": {
    "enabled": true,
    "allow_list": [
      "192.168.1.0/24",
      "10.0.0.0/8"
    ],
    "deny_list": [
      "192.168.1.100"
    ]
  }
}
```

#### Rate Limiting

```json
"security": {
  "rate_limiting": {
    "enabled": true,
    "requests_per_second": 1000,
    "burst_size": 100,
    "per_client": true
  }
}
```

### Performance Configuration

#### Connection Pooling

```json
"performance": {
  "connection_pooling": {
    "enabled": true,
    "max_connections": 1000,
    "idle_timeout": 300,
    "connection_timeout": 30
  }
}
```

#### Caching

```json
"performance": {
  "caching": {
    "enabled": true,
    "cache_size": "200MB",
    "ttl": 3600,
    "cache_attributes": true,
    "cache_directory_entries": true
  }
}
```

#### Threading

```json
"performance": {
  "threading": {
    "worker_threads": 4,
    "io_threads": 2,
    "max_requests_per_thread": 100
  }
}
```

### Logging Configuration

#### Basic Logging

```json
"logging": {
  "enable": true,
  "level": "info",  // "debug", "info", "warn", "error", "fatal"
  "log_file": "/var/log/simple-nfsd.log",
  "format": "json",  // "json" or "text"
  "rotation": true,
  "max_size": "100MB",
  "max_files": 10
}
```

#### Multiple Log Files

```json
"logging": {
  "enable": true,
  "level": "info",
  "access_log": "/var/log/simple-nfsd/access.log",
  "error_log": "/var/log/simple-nfsd/error.log",
  "audit_log": "/var/log/simple-nfsd/audit.log"
}
```

### Monitoring Configuration

#### Metrics

```json
"monitoring": {
  "metrics": {
    "enabled": true,
    "endpoint": "/metrics",
    "format": "prometheus",
    "interval": 60
  }
}
```

#### Health Checks

```json
"monitoring": {
  "health_checks": {
    "enabled": true,
    "endpoint": "/health",
    "interval": 30,
    "timeout": 5
  }
}
```

## NFS Protocol Configuration

### NFSv2 Support

```json
"protocols": {
  "nfsv2": {
    "enabled": true,
    "port": 2049,
    "max_file_size": "2GB"
  }
}
```

### NFSv3 Support

```json
"protocols": {
  "nfsv3": {
    "enabled": true,
    "port": 2049,
    "max_file_size": "2GB",
    "write_verifier": true
  }
}
```

### NFSv4 Support

```json
"protocols": {
  "nfsv4": {
    "enabled": true,
    "port": 2049,
    "minor_version": 1,
    "lease_time": 90,
    "grace_time": 90,
    "recovery_time": 10
  }
}
```

## Configuration Examples

### Home Network

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

### Office Network

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
    "security": {
      "authentication": "sys",
      "access_control": {
        "enabled": true,
        "allow_list": ["10.0.0.0/8"]
      }
    },
    "logging": {
      "enable": true,
      "level": "info",
      "format": "json"
    }
  }
}
```

### Enterprise Network

```json
{
  "nfs": {
    "listen": ["0.0.0.0:2049"],
    "exports": [
      {
        "path": "/var/enterprise/data",
        "clients": ["192.168.1.0/24"],
        "options": ["rw", "sync", "no_subtree_check"],
        "security": {
          "authentication": "krb5",
          "encryption": true
        },
        "quotas": {
          "enabled": true,
          "soft_limit": "10GB",
          "hard_limit": "20GB"
        }
      }
    ],
    "security": {
      "authentication": "krb5",
      "encryption": true,
      "rate_limiting": {
        "enabled": true,
        "requests_per_second": 1000
      }
    },
    "performance": {
      "caching": {
        "enabled": true,
        "cache_size": "500MB"
      },
      "threading": {
        "worker_threads": 8
      }
    },
    "logging": {
      "enable": true,
      "level": "info",
      "format": "json",
      "audit_log": "/var/log/simple-nfsd/audit.log"
    }
  }
}
```

## Configuration Validation

### Command Line Validation

```bash
# Validate configuration file
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Check configuration syntax
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --validate
```

### Common Validation Errors

1. **Invalid JSON syntax**: Check for missing commas, brackets, or quotes
2. **Invalid IP addresses**: Ensure IP addresses are in correct format
3. **Invalid export paths**: Ensure paths exist and are accessible
4. **Invalid client specifications**: Use correct IP ranges or hostname patterns
5. **Missing required fields**: Ensure all required fields are present

## Hot Reloading

Simple NFS Daemon supports configuration hot reloading:

```bash
# Reload configuration without restart
sudo systemctl reload simple-nfsd

# Or send SIGHUP signal
sudo kill -HUP $(pidof simple-nfsd)
```

## Best Practices

1. **Use descriptive export paths**: Make export paths meaningful
2. **Set appropriate access controls**: Use specific client specifications
3. **Enable logging**: Configure appropriate log levels for your environment
4. **Use quotas for large exports**: Set disk quotas to prevent abuse
5. **Test configurations**: Always validate configurations before deployment
6. **Backup configurations**: Keep configuration backups
7. **Monitor performance**: Use metrics and health checks
8. **Use security features**: Enable authentication and encryption when needed

## Troubleshooting Configuration

### Common Issues

1. **Configuration not loading**: Check file permissions and syntax
2. **Export not working**: Verify path exists and is accessible
3. **Clients can't mount**: Check client specifications and firewall
4. **Performance issues**: Review caching and threading settings

### Debug Commands

```bash
# Check configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Verbose logging
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf -v

# Show current configuration
simple-nfsd --show-config

# Show active exports
simple-nfsd --show-exports
```

## Next Steps

- [Configuration Examples](examples.md)
- [Security Configuration](security.md)
- [Troubleshooting Guide](../troubleshooting/README.md)
