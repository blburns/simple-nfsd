# Simplified VM Setup for Simple NFS Daemon

This document describes the simplified VM setup focused on Ubuntu and CentOS development environments, optimized for systems with limited resources.

## VM Directory Structure

VMs are now organized in the `virtuals/` directory following best practices:

```
virtuals/
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
./virtuals/vm-manager up ubuntu_dev

# Start CentOS VM
./virtuals/vm-manager up centos_dev

# SSH into Ubuntu VM
./virtuals/vm-manager ssh ubuntu_dev

# Run tests on both VMs
./virtuals/vm-manager matrix
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
cd virtuals/ubuntu_dev && vagrant up

# Start CentOS VM
cd virtuals/centos_dev && vagrant up
```

## Available Scripts

### VM Manager Script (`./virtuals/vm-manager`)
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
2. Create a new VM directory in `virtuals/`
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
- `virtuals/*/Vagrantfile`: VM-specific configurations

## Benefits of New Structure

1. **Clean Project Root**: VM files are isolated in `virtuals/` directory
2. **Better Organization**: Each VM has its own directory and configuration
3. **Easier Management**: VM manager script provides unified interface
4. **Version Control Friendly**: VM-specific files are properly ignored
5. **Scalable**: Easy to add new VMs without cluttering project root

## Notes

- The simplified setup reduces resource usage by focusing on essential development environments
- All additional VM configurations are commented out but can be easily re-enabled
- The automation tooling supports both alias names (ubuntu_dev, centos_dev) and full box names
- VM files (`.vagrant/`, logs) are excluded from version control

