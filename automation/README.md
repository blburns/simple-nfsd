# Simple NFS Daemon - Automation

This directory contains all automation scripts and configuration files for setting up and managing the simple-nfsd development environment.

## Directory Structure

```
automation/
├── playbook.yml              # Ansible playbook for VM setup
├── inventory.ini             # Ansible inventory file
├── requirements.yml          # Ansible Galaxy requirements
├── Makefile.vm               # Makefile for VM operations
├── scripts/                  # Shell scripts for VM operations
│   ├── vm-ssh               # SSH wrapper for VM
│   ├── vm-build             # Build script for VM
│   ├── vm-test              # Test script for VM
│   ├── vm-nfs-test          # NFS testing script
│   ├── setup-remote.sh      # Remote setup script
│   └── build.sh             # Build script
└── templates/               # Configuration templates
    ├── simple-nfsd.service.j2
    └── simple-nfsd.conf.j2
```

## Quick Start

### Using Makefile (Recommended)

```bash
# From project root
make -f automation/Makefile.vm vm-setup

# Test specific box
make -f automation/Makefile.vm vm-test-box BOX=ubuntu/jammy64

# Test multiple distributions
make -f automation/Makefile.vm vm-test-ubuntu
make -f automation/Makefile.vm vm-test-debian
make -f automation/Makefile.vm vm-test-centos
make -f automation/Makefile.vm vm-test-all
```

### Using Scripts Directly

   ```bash
# Start VM
   vagrant up

# Build project
./automation/scripts/vm-build

# Run tests
./automation/scripts/vm-test

# Test NFS functionality
./automation/scripts/vm-nfs-test
```

## Available Commands

### Makefile Targets

- `vm-up` - Start Vagrant VM
- `vm-down` - Stop Vagrant VM
- `vm-ssh` - SSH into VM
- `vm-build` - Build project on VM
- `vm-clean` - Clean build on VM
- `vm-test` - Run tests on VM
- `vm-test-filter FILTER="pattern"` - Run specific tests
- `vm-nfs-test` - Test NFS functionality
- `vm-status` - Check service status
- `vm-logs` - View service logs
- `vm-install` - Install and start service
- `vm-setup` - Full setup (up, build, test, install)
- `vm-dev` - Development workflow (build, test, install)

### Script Usage

#### vm-ssh
   ```bash
./automation/scripts/vm-ssh "command to run"
   ```

#### vm-build
   ```bash
./automation/scripts/vm-build [clean|test|install|status|logs]
   ```

#### vm-test
   ```bash
./automation/scripts/vm-test [test-pattern]
   ```

#### vm-nfs-test
```bash
./automation/scripts/vm-nfs-test
```

## Development Workflow

1. **Make changes** to your code locally
2. **Run build and test**:
   ```bash
   make -f automation/Makefile.vm vm-dev
   ```
3. **Test NFS functionality**:
   ```bash
   make -f automation/Makefile.vm vm-nfs-test
   ```

## VM Configuration

The Vagrant VM is configured with:
- **OS**: Ubuntu 22.04 LTS
- **Memory**: 2GB RAM
- **CPUs**: 2 cores
- **Network**: Private network (192.168.1.100)
- **Ports**: 2049 (NFS), 111 (RPC)

## Troubleshooting

### Common Issues

1. **VM won't start**: Check VirtualBox is installed and running
2. **Port conflicts**: Ensure ports 2049 and 111 are free
3. **Permission issues**: Scripts handle this automatically
4. **Build failures**: Check VM logs with `vagrant ssh` then `journalctl`

### Debug Commands

```bash
# Check VM status
vagrant status

# SSH into VM for debugging
vagrant ssh

# View VM logs
vagrant ssh -c "sudo journalctl -u simple-nfsd -f"

# Rebuild VM
vagrant destroy && vagrant up
```

## File Synchronization

The VM automatically syncs:
- **Source code** from project root to `/opt/simple-nfsd/`
- **Build directory** from `./build/` to `/opt/simple-nfsd/build/`

Changes are synced automatically when you run the scripts.

## Service Management

The simple-nfsd service runs as:
- **User**: nfsdev
- **Group**: nfsdev
- **Config**: /etc/simple-nfsd/simple-nfsd.conf
- **Logs**: /var/log/simple-nfsd/
- **Export**: /srv/nfs/

## Testing

### Unit Tests
```bash
make -f automation/Makefile.vm vm-test
```

### NFS Integration Tests
   ```bash
make -f automation/Makefile.vm vm-nfs-test
   ```

### Specific Test Patterns
   ```bash
make -f automation/Makefile.vm vm-test-filter FILTER="Nfsv2ProceduresTest.*"
```

## Multi-Box Testing

Test your NFS daemon across different Linux distributions using pre-built Vagrant boxes from [HashiCorp Vagrant Cloud](https://portal.cloud.hashicorp.com/vagrant/discover/blburns/).

### Quick Testing
   ```bash
# Test Ubuntu distributions
make -f automation/Makefile.vm vm-test-ubuntu

# Test Debian distributions  
make -f automation/Makefile.vm vm-test-debian

# Test CentOS/RHEL distributions
make -f automation/Makefile.vm vm-test-centos

# Test all common distributions
make -f automation/Makefile.vm vm-test-all
```

### Custom Box Testing
```bash
# Test specific box
make -f automation/Makefile.vm vm-test-box BOX=ubuntu/jammy64

# Test custom list of boxes
make -f automation/Makefile.vm vm-test-matrix BOXES="ubuntu/jammy64,debian/bullseye64,centos/8"

# Test with specific test pattern
make -f automation/Makefile.vm vm-test-matrix BOXES="ubuntu/jammy64" FILTER="*Nfsv2*"
```

### Available Boxes
See `automation/vagrant-boxes.yml` for a complete list of supported distributions including:
- **Ubuntu**: 18.04, 20.04, 22.04 LTS
- **Debian**: 11 (Bullseye), 12 (Bookworm)  
- **CentOS**: 8, 9 Stream
- **Rocky Linux**: 8, 9
- **AlmaLinux**: 8, 9
- **Fedora**: 38, 39
- **openSUSE**: Leap 15.4
- **Arch Linux**: Rolling release
- **Alpine**: 3.13 (minimal)

### Box Selection Tips
- **Ubuntu/Debian**: Best for development and testing
- **CentOS/RHEL**: Enterprise environments and production testing
- **Fedora**: Latest features and bleeding-edge testing
- **Alpine**: Minimal footprint and performance testing