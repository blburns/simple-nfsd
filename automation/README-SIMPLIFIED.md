# Simplified VM Setup for Simple NFS Daemon

This document describes the simplified VM setup focused on Ubuntu and CentOS development environments, optimized for systems with limited resources.

## VM Directory Structure

VMs are now organized in the `automation/vagrant/virtuals/` directory following best practices:

```
automation/vagrant/virtuals/
├── ubuntu_dev/          # Ubuntu 22.04 LTS development VM
│   └── Vagrantfile
├── centos_dev/          # CentOS 8 development VM
│   └── Vagrantfile
├── vm-manager          # VM management script
└── .gitignore          # Excludes VM-specific files
```

## Available Development VMs

The automation tooling focuses on two primary development VMs:

- **ubuntu_dev**: Ubuntu 22.04 LTS with development tools
- **centos_dev**: CentOS 8 with development tools

## Quick Start

### Method 1: Using VM Manager (Recommended)
```bash
# Start Ubuntu VM
./automation/vagrant/virtuals/vm-manager up ubuntu_dev

# Start CentOS VM
./automation/vagrant/virtuals/vm-manager up centos_dev

# SSH into Ubuntu VM
./automation/vagrant/virtuals/vm-manager ssh ubuntu_dev

# Run tests on both VMs
./automation/vagrant/virtuals/vm-manager matrix
```

### Method 2: Using Environment Variables
```bash
# Start Ubuntu Development VM
VAGRANT_BOX=ubuntu_dev vagrant up

# Start CentOS Development VM
VAGRANT_BOX=centos_dev vagrant up
```

### Method 3: Direct VM Directory Access
```bash
# Start Ubuntu VM
cd automation/vagrant/virtuals/ubuntu_dev && vagrant up

# Start CentOS VM
cd automation/vagrant/virtuals/centos_dev && vagrant up
```

## Available Scripts

### VM Manager Script (`./automation/vagrant/virtuals/vm-manager`)
- `up [vm_name]`: Start a VM
- `down [vm_name]`: Stop a VM
- `destroy [vm_name]`: Destroy a VM
- `status [vm_name]`: Show VM status
- `ssh [vm_name]`: SSH into a VM
- `build [vm_name]`: Build project on VM
- `test [vm_name]`: Run tests on VM
- `list`: List all available VMs
- `matrix`: Test all VMs

### Automation Scripts (`./automation/scripts/`)
- `vm-build [vm_name] [command]`: Build the application on specified VM
- `vm-test [vm_name] [test-pattern]`: Run tests on specified VM
- `vm-test-matrix`: Test both ubuntu_dev and centos_dev VMs
- `vm-ssh [vm_name] [command]`: SSH into specified VM or run command

## Examples

### Build on Ubuntu VM
```bash
./automation/scripts/vm-build ubuntu_dev
```

### Run specific test on CentOS VM
```bash
./automation/scripts/vm-test centos_dev "TestAuthManager*"
```

### SSH into Ubuntu VM
```bash
./automation/scripts/vm-ssh ubuntu_dev
```

## Expanding Testing

If you need to test additional distributions, you can:

1. Uncomment the desired boxes in `automation/vagrant-boxes.yml`
2. Create a new VM directory in `automation/vagrant/virtuals/`
3. Copy and customize a Vagrantfile template
4. Use the full box names with vm-test-matrix:
   ```bash
   ./automation/scripts/vm-test-matrix "ubuntu_dev,debian/bullseye64,centos/8"
   ```

## Configuration Files

- `automation/vagrant-boxes.yml`: VM box definitions (simplified)
- `automation/inventory.ini`: Ansible inventory (simplified)
- `automation/playbook.yml`: Ansible playbook for VM setup
- `Vagrantfile`: Main Vagrant configuration (project root)
- `automation/vagrant/virtuals/*/Vagrantfile`: VM-specific configurations

## Benefits of New Structure

1. **Clean Project Root**: VM files are isolated in `automation/vagrant/virtuals/` directory
2. **Better Organization**: Each VM has its own directory and configuration
3. **Easier Management**: VM manager script provides unified interface
4. **Version Control Friendly**: VM-specific files are properly ignored
5. **Scalable**: Easy to add new VMs without cluttering project root
6. **Headless Operation**: VMs run using VBoxHeadless (no GUI required)
7. **Resource Efficient**: Headless mode uses fewer system resources

## Headless Mode Requirements

The VMs are configured to run in headless mode using VBoxHeadless, which provides:

- **No GUI Required**: VMs run without opening VirtualBox GUI windows
- **Lower Resource Usage**: Reduced memory and CPU overhead
- **Better for Automation**: Perfect for CI/CD and automated testing
- **SSH Access**: All VM interaction is done via SSH

### Prerequisites

- VirtualBox installed (includes VBoxHeadless)
- VBoxManage command-line tool available
- SSH access to VMs (configured automatically)

## Networking Configuration

VMs are configured with private network interfaces for testing:

- **Ubuntu Dev VM**: `192.168.1.100`
- **CentOS Dev VM**: `192.168.1.101`
- **No Port Forwarding**: Services run directly on VM IPs for testing
- **Private Network**: VMs communicate on isolated network segment

### Testing Services

Services are accessible directly on the VM IPs:
- **NFS Server**: `192.168.1.100:2049` (Ubuntu) or `192.168.1.101:2049` (CentOS)
- **RPC Portmapper**: `192.168.1.100:111` (Ubuntu) or `192.168.1.101:111` (CentOS)
- **SSH Access**: `192.168.1.100:22` (Ubuntu) or `192.168.1.101:22` (CentOS)

## Shared Directories

Development files are automatically synchronized between host and VMs:

### Source Code Directories (Read-Write)
- `src/` → `/opt/simple-nfsd/src`
- `include/` → `/opt/simple-nfsd/include`
- `tests/` → `/opt/simple-nfsd/tests`
- `config/` → `/opt/simple-nfsd/config`

### Project Files (Available via /vagrant)
- `CMakeLists.txt` → `/vagrant/CMakeLists.txt`
- `Makefile` → `/vagrant/Makefile`
- All other project files → `/vagrant/`

### Build Directory (Read-Write)
- `build/` → `/opt/simple-nfsd/build`

### Benefits
- **Real-time Development**: Changes on host are immediately available in VM
- **Build Artifacts**: Compiled binaries are accessible from host
- **No Manual Sync**: Automatic synchronization via rsync
- **Cross-Platform Testing**: Build and test on different Linux distributions

## Development Workflow

### 1. Start Development VM
```bash
./automation/vagrant/virtuals/vm-manager up ubuntu_dev
```

### 2. Develop on Host
- Edit source files in `src/`, `include/`, `tests/` on your macOS host
- Changes are automatically synchronized to VM

### 3. Build and Test in VM
```bash
# SSH into VM
./automation/vagrant/virtuals/vm-manager ssh ubuntu_dev

# Build the application
cd /opt/simple-nfsd
sudo -u nfsdev ./build.sh

# Run tests
cd /opt/simple-nfsd/build
sudo -u nfsdev make test

# Test NFS functionality
sudo systemctl start simple-nfsd
sudo systemctl status simple-nfsd
```

### 4. Access Build Artifacts
- Compiled binaries are available in `./build/` on your host
- Test results and logs are accessible from both host and VM

### 5. Test Services
```bash
# Test NFS from another VM or host
mount -t nfs4 192.168.1.100:/srv/nfs /mnt/nfs-test
```

## Notes

- The simplified setup reduces resource usage by focusing on essential development environments
- All additional VM configurations are commented out but can be easily re-enabled
- The automation tooling supports both alias names (ubuntu_dev, centos_dev) and full box names
- VM files (`.vagrant/`, logs) are excluded from version control
- VMs run in headless mode for better resource efficiency

