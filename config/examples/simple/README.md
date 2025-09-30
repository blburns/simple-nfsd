# Simple Configuration Examples

This directory contains basic configuration examples for Simple NFS Daemon, suitable for development and testing environments.

## Files

- `simple-nfsd.conf` - INI format configuration
- `simple-nfsd.json` - JSON format configuration  
- `simple-nfsd.yaml` - YAML format configuration

## Features

- **Basic NFS Protocol Support**: NFSv2 and NFSv3
- **Simple Security**: AUTH_SYS authentication only
- **Minimal Performance Tuning**: Default buffer sizes
- **Basic Export Configuration**: Simple read/write shares
- **Standard Logging**: INFO level logging

## Usage

Copy one of the configuration files to your system and customize as needed:

```bash
# Copy INI configuration
sudo cp simple-nfsd.conf /etc/simple-nfsd/

# Copy JSON configuration
sudo cp simple-nfsd.json /etc/simple-nfsd/

# Copy YAML configuration
sudo cp simple-nfsd.yaml /etc/simple-nfsd/
```

## Customization

- Change `bind_address` to bind to specific network interface
- Modify `port` if you need to use a different port
- Update `root_path` to point to your shared directory
- Add/remove exports in the `[exports]` section
- Adjust logging level in the `[logging]` section

## Security Notes

- This configuration uses basic AUTH_SYS authentication
- Root squash is enabled for security
- Anonymous access is configured for public shares
- Consider using production configuration for sensitive data
