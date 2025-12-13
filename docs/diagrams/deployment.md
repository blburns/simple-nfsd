# Simple NFS Daemon - Deployment Diagrams

## Basic Deployment Architecture

```mermaid
graph TB
    subgraph "NFS Clients"
        Client1[NFS Client 1]
        Client2[NFS Client 2]
        ClientN[NFS Client N]
    end

    subgraph "NFS Server"
        Server[simple-nfsd<br/>Main Process]
        Config[/etc/simple-nfsd/<br/>Configuration]
        Exports[/etc/exports<br/>Export Definitions]
        Logs[/var/log/simple-nfsd/<br/>NFS Logs]
    end

    subgraph "Exported Filesystems"
        Export1[/export/share1<br/>Export 1]
        Export2[/export/share2<br/>Export 2]
        ExportN[/export/shareN<br/>Export N]
    end

    subgraph "System Services"
        Systemd[systemd<br/>Service Manager]
        Rpcbind[rpcbind<br/>RPC Port Mapper]
    end

    Client1 --> Server
    Client2 --> Server
    ClientN --> Server

    Systemd --> Server
    Systemd --> Config
    Systemd --> Rpcbind

    Server --> Config
    Server --> Exports
    Server --> Logs
    Server --> Export1
    Server --> Export2
    Server --> ExportN
```

## NFS High Availability Deployment

```mermaid
graph TB
    subgraph "Load Balancer"
        LB[Load Balancer<br/>NFS Connection Distribution]
    end

    subgraph "NFS Server Pool"
        Server1[simple-nfsd<br/>Server 1]
        Server2[simple-nfsd<br/>Server 2]
        Server3[simple-nfsd<br/>Server 3]
    end

    subgraph "Shared Storage"
        SharedStorage[Shared Storage<br/>NFS/GlusterFS]
    end

    subgraph "Clients"
        Client1[Client 1]
        Client2[Client 2]
        ClientN[Client N]
    end

    Client1 --> LB
    Client2 --> LB
    ClientN --> LB

    LB --> Server1
    LB --> Server2
    LB --> Server3

    Server1 --> SharedStorage
    Server2 --> SharedStorage
    Server3 --> SharedStorage
```
