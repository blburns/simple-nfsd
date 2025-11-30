#!/bin/bash
# Pre-uninstallation script for simple-nfsd RPM

set -e

PROJECT_NAME="simple-nfsd"

# Stop service before removal
if [ "$1" -eq 0 ]; then
    systemctl stop "$simple-nfsd" 2>/dev/null || true
    systemctl disable "$simple-nfsd" 2>/dev/null || true
fi

exit 0

