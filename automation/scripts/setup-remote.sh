#!/bin/bash
# Simple NFS Daemon - Remote Setup Script
# Run this on your target machine

set -e

echo "=== Simple NFS Daemon Setup ==="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root or with sudo"
    exit 1
fi

# Update package cache
echo "Updating package cache..."
if command -v apt-get &> /dev/null; then
    apt-get update
    INSTALL_CMD="apt-get install -y"
elif command -v yum &> /dev/null; then
    INSTALL_CMD="yum install -y"
elif command -v dnf &> /dev/null; then
    INSTALL_CMD="dnf install -y"
else
    echo "Unsupported package manager"
    exit 1
fi

# Install dependencies
echo "Installing build dependencies..."
$INSTALL_CMD build-essential cmake git pkg-config libssl-dev libjsoncpp-dev libyaml-cpp-dev libgtest-dev libgmock-dev nfs-common nfs-kernel-server python3 python3-pip

# Create build user
echo "Creating build user..."
useradd -m -s /bin/bash nfsdev || true
usermod -aG sudo nfsdev || true

# Create project directory
echo "Setting up project directory..."
PROJECT_DIR="/opt/simple-nfsd"
mkdir -p $PROJECT_DIR
chown nfsdev:nfsdev $PROJECT_DIR

# Create log directories
echo "Creating log directories..."
mkdir -p /var/log/simple-nfsd
chown nfsdev:nfsdev /var/log/simple-nfsd

# Create config directory
echo "Creating config directory..."
mkdir -p /etc/simple-nfsd
chown nfsdev:nfsdev /etc/simple-nfsd

# Create NFS export directory
echo "Creating NFS export directory..."
mkdir -p /srv/nfs
chmod 755 /srv/nfs

# Configure NFS exports
echo "Configuring NFS exports..."
echo "/srv/nfs *(rw,sync,no_subtree_check,no_root_squash)" >> /etc/exports

# Enable NFS services
echo "Enabling NFS services..."
systemctl enable rpcbind
systemctl enable nfs-server
systemctl start rpcbind
systemctl start nfs-server

echo "=== Setup Complete ==="
echo "Next steps:"
echo "1. Copy the project files to $PROJECT_DIR"
echo "2. Run: cd $PROJECT_DIR && sudo -u nfsdev ./build.sh"
echo "3. Test with: sudo -u nfsdev make test"
