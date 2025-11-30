Name:           simple-nfsd
Version:        ${VERSION}
Release:        1%{?dist}
Summary:        simple-nfsd - A lightweight and secure daemon
License:        Apache-2.0
URL:            https://github.com/simpledaemons/%{name}
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.10
BuildRequires:  gcc-c++
BuildRequires:  openssl-devel
BuildRequires:  jsoncpp-devel
Requires:       openssl-libs
Requires:       jsoncpp

%description
simple-nfsd is a modern, secure implementation of the NFS protocol.

Features:
* High performance
* Secure by default
* Easy configuration
* Systemd integration

%prep
%setup -q

%build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make %{?_smp_mflags}

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/%{name}
mkdir -p %{buildroot}%{_localstatedir}/log/%{name}
mkdir -p %{buildroot}%{_localstatedir}/lib/%{name}
mkdir -p %{buildroot}%{_unitdir}

install -m 755 build/%{name} %{buildroot}%{_bindir}/%{name}
install -m 644 config/%{name}.conf %{buildroot}%{_sysconfdir}/%{name}/%{name}.conf.example
install -m 644 deployment/systemd/%{name}.service %{buildroot}%{_unitdir}/%{name}.service

%pre
# License acceptance check
if [ "$1" -eq 1 ]; then
    echo "=========================================="
    echo "simple-nfsd License Agreement"
    echo "=========================================="
    echo ""
    cat %{_datadir}/doc/%{name}/LICENSE 2>/dev/null || \
        echo "Please review the license at: https://github.com/simpledaemons/%{name}/blob/main/LICENSE"
    echo ""
    echo "By continuing, you agree to the terms of the license."
    echo ""
    read -p "Do you accept the license? [y/N]: " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "License not accepted. Installation cancelled."
        exit 1
    fi
    
    # Create service user
    if ! id nfsddev &>/dev/null; then
        useradd -r -s /sbin/nologin -d /var/lib/%{name} -c "%{name} service user" nfsddev
    fi
fi

%post
systemctl daemon-reload
systemctl enable %{name} 2>/dev/null || true
systemctl start %{name} 2>/dev/null || true

%preun
if [ "$1" -eq 0 ]; then
    systemctl stop %{name} 2>/dev/null || true
    systemctl disable %{name} 2>/dev/null || true
fi

%postun
systemctl daemon-reload

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%config(noreplace) %{_sysconfdir}/%{name}/%{name}.conf.example
%{_unitdir}/%{name}.service
%doc LICENSE README.md
%license LICENSE

%changelog
* $(date +"%a %b %d %Y") SimpleDaemons <support@simpledaemons.com> - ${VERSION}-1
- Initial package release

