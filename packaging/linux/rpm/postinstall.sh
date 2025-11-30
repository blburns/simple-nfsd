#!/bin/bash
# Post-installation script for simple-nfsd RPM

set -e

PROJECT_NAME="simple-nfsd"
SERVICE_USER="nfsddev"

# Create service user if it doesn't exist
if ! id "$SERVICE_USER" &>/dev/null; then
    useradd -r -s /sbin/nologin -d /var/lib/$simple-nfsd -c "$simple-nfsd service user" "$SERVICE_USER"
fi

# Set ownership
chown -R "$SERVICE_USER:$SERVICE_USER" /etc/$simple-nfsd 2>/dev/null || true
chown -R "$SERVICE_USER:$SERVICE_USER" /var/log/$simple-nfsd 2>/dev/null || true
chown -R "$SERVICE_USER:$SERVICE_USER" /var/lib/$simple-nfsd 2>/dev/null || true

# Enable and start service
systemctl daemon-reload
systemctl enable "$simple-nfsd" 2>/dev/null || true
systemctl start "$simple-nfsd" 2>/dev/null || true

exit 0

