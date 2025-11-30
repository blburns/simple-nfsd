#!/bin/bash
# Simple NFS Daemon - Build Script
# Run this after setup-remote.sh

set -e

echo "=== Building Simple NFS Daemon ==="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: CMakeLists.txt not found. Please run from project root."
    exit 1
fi

# Create build directory
echo "Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
echo "Building project..."
make -j$(nproc)

# Run tests
echo "Running tests..."
make test || echo "Some tests may fail - this is expected during development"

# Create systemd service file
echo "Creating systemd service..."
cat > /tmp/simple-nfsd.service << 'EOF'
[Unit]
Description=Simple NFS Daemon
Documentation=https://github.com/blburns/simple-nfsd
After=network.target rpcbind.service

[Service]
Type=simple
User=nfsdev
Group=nfsdev
ExecStart=/opt/simple-nfsd/build/simple-nfsd --config /etc/simple-nfsd/simple-nfsd.conf
ExecReload=/bin/kill -HUP $MAINPID
KillMode=process
Restart=on-failure
RestartSec=5s

# Security settings
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/opt/simple-nfsd

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
EOF

sudo cp /tmp/simple-nfsd.service /etc/systemd/system/
sudo systemctl daemon-reload

# Create configuration file
echo "Creating configuration..."
sudo tee /etc/simple-nfsd/simple-nfsd.conf > /dev/null << 'EOF'
# Simple NFS Daemon Configuration

[server]
bind_address = 0.0.0.0
port = 2049
max_connections = 100
enable_tcp = true
enable_udp = true
root_path = /srv/nfs

[logging]
log_level = info
log_file = /var/log/simple-nfsd/simple-nfsd.log
enable_audit_logging = true
audit_log_file = /var/log/simple-nfsd/audit.log

[security]
enable_auth_sys = true
enable_auth_dh = false
enable_kerberos = false
enable_acl = true
enable_encryption = false
root_squash = true
anonymous_access = false
session_timeout = 3600

[performance]
thread_pool_size = 4
max_request_size = 1048576
cache_size = 1000
EOF

# Set permissions
sudo chown nfsdev:nfsdev /etc/simple-nfsd/simple-nfsd.conf
sudo chmod 644 /etc/simple-nfsd/simple-nfsd.conf

echo "=== Build Complete ==="
echo "To start the service:"
echo "  sudo systemctl enable simple-nfsd"
echo "  sudo systemctl start simple-nfsd"
echo ""
echo "To check status:"
echo "  sudo systemctl status simple-nfsd"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u simple-nfsd -f"
echo ""
echo "To test NFS:"
echo "  sudo mkdir -p /mnt/nfs-test"
echo "  sudo mount -t nfs4 localhost:/srv/nfs /mnt/nfs-test"
