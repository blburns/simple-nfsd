# simple-nfsd - Automation

This directory contains all automation scripts and configuration files for setting up and managing the simple-nfsd development environment.

## Directory Structure

```
automation/
├── ansible/                  # Ansible automation
│   ├── playbook.yml         # Ansible playbook for VM setup
│   ├── inventory.ini        # Ansible inventory file
│   ├── requirements.yml     # Ansible Galaxy requirements
│   ├── Makefile.vm          # Makefile for VM operations
│   ├── vagrant-boxes.yml   # Vagrant box configurations
│   ├── scripts/             # Shell scripts for VM operations
│   │   ├── vm-ssh          # SSH wrapper for VM
│   │   ├── vm-build        # Build script for VM
│   │   ├── vm-test         # Test script for VM
│   │   ├── setup-remote.sh # Remote setup script
│   │   └── build.sh        # Build script
│   └── templates/          # Configuration templates
├── ci/                      # CI/CD configuration
│   ├── Jenkinsfile         # Jenkins pipeline
│   └── .travis.yml         # Travis CI configuration
├── docker/                  # Docker configuration
│   ├── Dockerfile          # Docker image definition
│   ├── docker-compose.yml  # Docker Compose configuration
│   └── examples/           # Docker examples
└── vagrant/                 # Vagrant configuration
    ├── Vagrantfile         # Main Vagrantfile
    └── virtuals/           # Multi-VM configurations
        ├── ubuntu_dev/
        └── centos_dev/
```

## Quick Start

### Using Docker

```bash
# Build and run with Docker Compose
cd automation/docker
docker-compose up -d

# Or from project root
docker-compose -f automation/docker/docker-compose.yml up -d
```

### Using Vagrant

```bash
# Start VM
cd automation/vagrant
vagrant up

# SSH into VM
vagrant ssh

# Build project
./automation/ansible/scripts/vm-build
```

### Using Ansible

```bash
# Run playbook
ansible-playbook -i automation/ansible/inventory.ini automation/ansible/playbook.yml
```

## CI/CD

### Jenkins

The Jenkins pipeline is located at `automation/ci/Jenkinsfile`. It supports:
- Multi-platform builds (Linux, macOS, Windows)
- Automated testing
- Static analysis
- Package generation
- Docker image building

### Travis CI

The Travis CI configuration is located at `automation/ci/.travis.yml`. It provides:
- Automated builds on push
- Multi-platform testing
- Code coverage reporting

## Docker

Docker files are located in `automation/docker/`:
- `Dockerfile` - Multi-stage build for different distributions
- `docker-compose.yml` - Development and production configurations
- `examples/` - Example Docker configurations

## Vagrant

Vagrant configuration is in `automation/vagrant/`:
- `Vagrantfile` - Main Vagrant configuration
- `virtuals/` - Multi-VM configurations for different distributions

## Ansible

Ansible automation is in `automation/ansible/`:
- `playbook.yml` - Main playbook for environment setup
- `inventory.ini` - Host inventory
- `requirements.yml` - Ansible Galaxy dependencies
- `scripts/` - Helper scripts for VM operations
- `templates/` - Configuration templates

---

*For detailed documentation, see the individual README files in each subdirectory.*

