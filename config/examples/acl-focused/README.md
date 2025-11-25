# ACL-Focused Configuration Examples

This directory contains configuration examples specifically focused on NFSv4 ACL (Access Control List) management and file access tracking features.

## Features

- **Full NFSv4 ACL Support**: Complete GETACL/SETACL procedure implementation
- **Advanced File Access Tracking**: Stateful tracking of file access modes and sharing modes
- **ACL Management**: Default ACLs, ACL inheritance, and ACL caching
- **File Access Modes**: READ_ONLY, WRITE_ONLY, READ_WRITE, APPEND
- **File Sharing Modes**: EXCLUSIVE, SHARED_READ, SHARED_WRITE, SHARED_ALL
- **Conflict Detection**: Automatic detection of sharing mode conflicts
- **ACL Caching**: Performance optimization through ACL caching

## Use Cases

- **Multi-user Environments**: Where fine-grained access control is required
- **Collaborative Workspaces**: Where file sharing modes need to be tracked
- **Security-Critical Applications**: Where ACL auditing is important
- **Enterprise Deployments**: Where access control policies must be enforced

## Configuration Files

- `simple-nfsd.json` - JSON format with full ACL configuration
- `simple-nfsd.yaml` - YAML format with ACL examples
- `simple-nfsd.ini` - INI format with ACL settings

## ACL Configuration

### Basic ACL Setup

```json
{
  "security": {
    "enable_acl": true
  },
  "acl_management": {
    "enabled": true,
    "inherit_acls": true,
    "acl_cache": true,
    "acl_cache_ttl": 300
  }
}
```

### Default ACL

```json
{
  "acl_management": {
    "default_acl": {
      "entries": [
        {
          "type": 1,
          "id": 0,
          "permissions": 7,
          "name": "owner"
        },
        {
          "type": 2,
          "id": 0,
          "permissions": 5,
          "name": "group"
        },
        {
          "type": 4,
          "id": 0,
          "permissions": 5,
          "name": "others"
        }
      ]
    }
  }
}
```

## File Access Tracking

### Basic Tracking

```json
{
  "file_access_tracking": {
    "enabled": true,
    "timeout": 3600
  }
}
```

### Advanced Tracking

```json
{
  "file_access_tracking": {
    "enabled": true,
    "timeout": 7200,
    "track_modes": ["READ_ONLY", "WRITE_ONLY", "READ_WRITE", "APPEND"],
    "track_sharing": ["EXCLUSIVE", "SHARED_READ", "SHARED_WRITE", "SHARED_ALL"],
    "cleanup_interval": 60,
    "conflict_detection": true,
    "logging": true
  }
}
```

## Usage

1. **Review Configuration**: Understand ACL and file access tracking settings
2. **Customize ACLs**: Set up default ACLs for your use case
3. **Configure Tracking**: Enable appropriate access and sharing mode tracking
4. **Test**: Validate ACL operations and file access tracking
5. **Deploy**: Use in production with monitoring

## Best Practices

- Enable ACL caching for better performance
- Use appropriate timeout values for file access tracking
- Configure conflict detection for shared file access
- Monitor ACL operations for security auditing
- Test ACL inheritance behavior
- Review file access tracking logs regularly

## Troubleshooting

- **ACL not applying**: Check ACL cache and refresh if needed
- **File access conflicts**: Review sharing mode configuration
- **Performance issues**: Adjust ACL cache TTL and cleanup intervals
- **Tracking not working**: Verify file access tracking is enabled

