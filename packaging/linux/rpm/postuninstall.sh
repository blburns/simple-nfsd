#!/bin/bash
# Post-uninstallation script for simple-nfsd RPM

set -e

# Reload systemd
systemctl daemon-reload

exit 0

