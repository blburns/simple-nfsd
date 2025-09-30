# Troubleshooting Guide

This guide helps you diagnose and resolve common issues with Simple NFS Daemon.

## Quick Diagnostics

### Check Service Status

```bash
# Check if service is running
sudo systemctl status simple-nfsd

# Check process
ps aux | grep simple-nfsd

# Check port
sudo netstat -tulpn | grep :2049
```

### Check Logs

```bash
# System logs
sudo journalctl -u simple-nfsd -f

# Application logs
sudo tail -f /var/log/simple-nfsd.log

# Error logs
sudo tail -f /var/log/simple-nfsd/error.log
```

### Test Configuration

```bash
# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Show current configuration
simple-nfsd --show-config

# Show active exports
simple-nfsd --show-exports
```

## Common Issues

### Service Won't Start

#### Permission Denied

**Symptoms:**
- Service fails to start
- Permission denied errors in logs

**Solutions:**
```bash
# Check file permissions
ls -la /etc/simple-nfsd/
sudo chown -R nfsd:nfsd /etc/simple-nfsd/

# Check if running as root
sudo simple-nfsd -f

# Check user exists
id nfsd
```

#### Port Already in Use

**Symptoms:**
- "Address already in use" error
- Service fails to bind to port 2049

**Solutions:**
```bash
# Check what's using port 2049
sudo netstat -tulpn | grep :2049
sudo lsof -i :2049

# Stop conflicting service
sudo systemctl stop nfs-server
sudo systemctl stop rpcbind

# Kill process using port
sudo fuser -k 2049/tcp
```

#### Configuration Errors

**Symptoms:**
- Configuration validation fails
- Service starts but immediately exits

**Solutions:**
```bash
# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Check JSON syntax
python -m json.tool /etc/simple-nfsd/simple-nfsd.conf

# Check YAML syntax
python -c "import yaml; yaml.safe_load(open('/etc/simple-nfsd/simple-nfsd.conf'))"
```

### Clients Can't Mount

#### Mount Failed: Connection Refused

**Symptoms:**
- `mount.nfs: Connection refused`
- Client can't connect to server

**Solutions:**
```bash
# Check if NFS service is running
sudo systemctl status simple-nfsd

# Check if port is listening
sudo netstat -tulpn | grep :2049

# Check firewall
sudo ufw status
sudo firewall-cmd --list-all

# Test connectivity
telnet localhost 2049
```

#### Mount Failed: Permission Denied

**Symptoms:**
- `mount.nfs: Permission denied`
- Client connects but can't mount

**Solutions:**
```bash
# Check exports
simple-nfsd --show-exports

# Check client IP in exports
grep -r "192.168.1" /etc/simple-nfsd/

# Check export permissions
ls -la /var/lib/simple-nfsd/shares
```

#### Mount Failed: No Such File or Directory

**Symptoms:**
- `mount.nfs: No such file or directory`
- Export path doesn't exist

**Solutions:**
```bash
# Check if export path exists
ls -la /var/lib/simple-nfsd/shares

# Create export directory
sudo mkdir -p /var/lib/simple-nfsd/shares
sudo chown nfsd:nfsd /var/lib/simple-nfsd/shares
sudo chmod 755 /var/lib/simple-nfsd/shares
```

### Performance Issues

#### Slow File Operations

**Symptoms:**
- Slow read/write operations
- High latency

**Solutions:**
```bash
# Check statistics
simple-nfsd --stats

# Check system resources
top
htop
iostat -x 1

# Check network
iftop
nethogs

# Optimize configuration
# - Enable caching
# - Increase worker threads
# - Tune buffer sizes
```

#### High Memory Usage

**Symptoms:**
- High memory consumption
- System running out of memory

**Solutions:**
```bash
# Check memory usage
free -h
ps aux --sort=-%mem | head

# Check for memory leaks
valgrind --tool=memcheck simple-nfsd

# Optimize configuration
# - Reduce cache size
# - Limit connections
# - Enable memory limits
```

### Security Issues

#### Authentication Failures

**Symptoms:**
- Authentication errors in logs
- Clients can't authenticate

**Solutions:**
```bash
# Check authentication configuration
grep -r "authentication" /etc/simple-nfsd/

# Check Kerberos (if using)
klist
kinit

# Check system authentication
getent passwd nfsd
```

#### Access Denied

**Symptoms:**
- Access denied errors
- Clients can't access files

**Solutions:**
```bash
# Check file permissions
ls -la /var/lib/simple-nfsd/shares

# Check export options
grep -r "options" /etc/simple-nfsd/

# Check client access
grep -r "clients" /etc/simple-nfsd/
```

## Debugging Techniques

### Enable Debug Logging

```bash
# Start with debug logging
sudo simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf -f -v

# Or modify configuration
{
  "nfs": {
    "logging": {
      "enable": true,
      "level": "debug"
    }
  }
}
```

### Network Debugging

```bash
# Check network connectivity
ping localhost
telnet localhost 2049

# Check routing
ip route
route -n

# Check DNS
nslookup localhost
dig localhost
```

### File System Debugging

```bash
# Check file system
df -h
lsblk

# Check inodes
df -i

# Check file permissions
ls -la /var/lib/simple-nfsd/shares
getfacl /var/lib/simple-nfsd/shares
```

### Process Debugging

```bash
# Check process details
ps aux | grep simple-nfsd
lsof -p $(pidof simple-nfsd)

# Check system calls
strace -p $(pidof simple-nfsd)

# Check memory usage
pmap $(pidof simple-nfsd)
```

## Log Analysis

### Common Log Messages

#### Info Messages
```
[INFO] Simple NFS Daemon started
[INFO] Listening on 0.0.0.0:2049
[INFO] Export added: /var/lib/simple-nfsd/shares
[INFO] Client connected: 192.168.1.100
```

#### Warning Messages
```
[WARN] Configuration reloaded
[WARN] High memory usage: 85%
[WARN] Connection limit reached: 1000
```

#### Error Messages
```
[ERROR] Failed to bind to port 2049: Address already in use
[ERROR] Export path not found: /var/lib/simple-nfsd/shares
[ERROR] Permission denied: /var/lib/simple-nfsd/shares
[ERROR] Client authentication failed: 192.168.1.100
```

### Log Filtering

```bash
# Filter by level
sudo journalctl -u simple-nfsd | grep ERROR
sudo journalctl -u simple-nfsd | grep WARN

# Filter by time
sudo journalctl -u simple-nfsd --since "1 hour ago"
sudo journalctl -u simple-nfsd --since "2024-01-01" --until "2024-01-02"

# Filter by client
sudo journalctl -u simple-nfsd | grep "192.168.1.100"
```

## Performance Tuning

### System Tuning

```bash
# Increase file descriptor limits
echo "* soft nofile 65536" | sudo tee -a /etc/security/limits.conf
echo "* hard nofile 65536" | sudo tee -a /etc/security/limits.conf

# Tune network parameters
echo "net.core.rmem_max = 16777216" | sudo tee -a /etc/sysctl.conf
echo "net.core.wmem_max = 16777216" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### Application Tuning

```json
{
  "nfs": {
    "performance": {
      "threading": {
        "worker_threads": 8,
        "io_threads": 4
      },
      "caching": {
        "enabled": true,
        "cache_size": "500MB"
      },
      "connection_pooling": {
        "max_connections": 2000
      }
    }
  }
}
```

## Recovery Procedures

### Service Recovery

```bash
# Restart service
sudo systemctl restart simple-nfsd

# Reload configuration
sudo systemctl reload simple-nfsd

# Reset to defaults
sudo systemctl stop simple-nfsd
sudo cp /etc/simple-nfsd/simple-nfsd.conf.backup /etc/simple-nfsd/simple-nfsd.conf
sudo systemctl start simple-nfsd
```

### Data Recovery

```bash
# Check file system
sudo fsck /dev/sda1

# Restore from backup
sudo cp -r /backup/shares/* /var/lib/simple-nfsd/shares/
sudo chown -R nfsd:nfsd /var/lib/simple-nfsd/shares
```

### Configuration Recovery

```bash
# Restore configuration
sudo cp /etc/simple-nfsd/simple-nfsd.conf.backup /etc/simple-nfsd/simple-nfsd.conf

# Validate configuration
simple-nfsd -c /etc/simple-nfsd/simple-nfsd.conf --test-config

# Restart service
sudo systemctl restart simple-nfsd
```

## Getting Help

### Self-Service Resources

1. **Check logs**: Always check logs first
2. **Validate configuration**: Use `--test-config`
3. **Check documentation**: Review relevant guides
4. **Search issues**: Check GitHub issues

### Community Support

1. **GitHub Issues**: [Report bugs and ask questions](https://github.com/SimpleDaemons/simple-nfsd/issues)
2. **Discussions**: [Community discussions](https://github.com/SimpleDaemons/simple-nfsd/discussions)
3. **Documentation**: [Full documentation](https://github.com/SimpleDaemons/simple-nfsd/docs)

### Professional Support

1. **Email**: support@simpledaemons.com
2. **Phone**: +1-800-SIMPLE-1
3. **Enterprise**: enterprise@simpledaemons.com

### Reporting Issues

When reporting issues, include:

1. **Version**: `simple-nfsd --version`
2. **Configuration**: `simple-nfsd --show-config`
3. **Logs**: Relevant log entries
4. **Steps to reproduce**: Detailed steps
5. **Expected behavior**: What should happen
6. **Actual behavior**: What actually happens

## Prevention

### Best Practices

1. **Regular backups**: Backup configurations and data
2. **Monitoring**: Set up monitoring and alerting
3. **Testing**: Test configurations before deployment
4. **Documentation**: Keep configuration documentation
5. **Updates**: Keep system and application updated

### Monitoring Setup

```bash
# Set up log monitoring
sudo apt install logwatch
sudo vim /etc/logwatch/conf/logwatch.conf

# Set up performance monitoring
sudo apt install htop iotop nethogs

# Set up alerting
sudo apt install mailutils
```

## Next Steps

- [Configuration Guide](../configuration/README.md)
- [Performance Tuning](performance.md)
- [Security Guide](../config/SECURITY_CONFIG.md)
- [Monitoring Guide](../user-guide/monitoring.md)
