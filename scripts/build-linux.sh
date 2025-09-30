#!/bin/bash
# Generic Linux build script for simple-nfsd
# Automatically detects distribution and uses appropriate package manager

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
DIST_DIR="$PROJECT_ROOT/dist"

# Build options
BUILD_TYPE="Release"
BUILD_SHARED_LIBS="OFF"
BUILD_TESTS="ON"
ENABLE_SSL="ON"
ENABLE_JSON="ON"
ENABLE_STATIC_LINKING="OFF"
PACKAGE="false"
INSTALL="false"
SERVICE="false"
CLEAN="false"

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to detect Linux distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO_ID="$ID"
        DISTRO_NAME="$NAME"
        DISTRO_VERSION="$VERSION_ID"
        print_status "Detected: $DISTRO_NAME $DISTRO_VERSION ($DISTRO_ID)"
        
        # Map distribution ID to package manager
        case "$DISTRO_ID" in
            "ubuntu"|"debian"|"linuxmint"|"pop")
                PACKAGE_MANAGER="apt"
                print_status "Using APT package manager"
                ;;
            "rhel"|"centos"|"rocky"|"alma"|"fedora"|"amzn")
                if command_exists dnf; then
                    PACKAGE_MANAGER="dnf"
                else
                    PACKAGE_MANAGER="yum"
                fi
                print_status "Using $PACKAGE_MANAGER package manager"
                ;;
            "arch"|"manjaro"|"endeavouros")
                PACKAGE_MANAGER="pacman"
                print_status "Using Pacman package manager"
                ;;
            "opensuse"|"sles")
                PACKAGE_MANAGER="zypper"
                print_status "Using Zypper package manager"
                ;;
            *)
                print_warning "Unknown distribution: $DISTRO_ID"
                print_status "Attempting to auto-detect package manager..."
                if command_exists apt; then
                    PACKAGE_MANAGER="apt"
                elif command_exists dnf; then
                    PACKAGE_MANAGER="dnf"
                elif command_exists yum; then
                    PACKAGE_MANAGER="yum"
                elif command_exists pacman; then
                    PACKAGE_MANAGER="pacman"
                elif command_exists zypper; then
                    PACKAGE_MANAGER="zypper"
                else
                    print_error "Could not determine package manager"
                    exit 1
                fi
                print_status "Auto-detected: $PACKAGE_MANAGER"
                ;;
        esac
    else
        print_error "Could not detect distribution"
        exit 1
    fi
}

# Function to install dependencies based on package manager
install_dependencies() {
    print_status "Installing build dependencies using $PACKAGE_MANAGER..."
    
    case "$PACKAGE_MANAGER" in
        "apt")
            # Update package list
            sudo apt-get update
            
            # Install build tools
            sudo apt-get install -y \
                build-essential \
                cmake \
                pkg-config \
                git \
                wget \
                curl
            
            # Install development libraries
            sudo apt-get install -y \
                libssl-dev \
                libjsoncpp-dev \
                libpthread-stubs0-dev
            
            # Install optional development tools
            sudo apt-get install -y \
                clang-format \
                cppcheck \
                valgrind \
                gdb \
                lcov
            ;;
        "dnf"|"yum")
            # Enable EPEL repository for additional packages
            if command_exists dnf; then
                sudo dnf install -y epel-release
            else
                sudo yum install -y epel-release
            fi
            
            # Install build tools
            if command_exists dnf; then
                sudo dnf groupinstall -y "Development Tools"
                sudo dnf install -y \
                    cmake \
                    pkgconfig \
                    git \
                    wget \
                    curl
            else
                sudo yum groupinstall -y "Development Tools"
                sudo yum install -y \
                    cmake \
                    pkgconfig \
                    git \
                    wget \
                    curl
            fi
            
            # Install development libraries
            if command_exists dnf; then
                sudo dnf install -y \
                    openssl-devel \
                    jsoncpp-devel
            else
                sudo yum install -y \
                    openssl-devel \
                    jsoncpp-devel
            fi
            
            # Install optional development tools
            if command_exists dnf; then
                sudo dnf install -y \
                    clang-tools-extra \
                    cppcheck \
                    valgrind \
                    gdb \
                    lcov
            else
                sudo yum install -y \
                    clang-tools-extra \
                    cppcheck \
                    valgrind \
                    gdb \
                    lcov
            fi
            ;;
        "pacman")
            # Update package database
            sudo pacman -Sy
            
            # Install build tools
            sudo pacman -S --needed --noconfirm \
                base-devel \
                cmake \
                pkgconf \
                git \
                wget \
                curl
            
            # Install development libraries
            sudo pacman -S --needed --noconfirm \
                openssl \
                jsoncpp
            
            # Install optional development tools
            sudo pacman -S --needed --noconfirm \
                clang \
                cppcheck \
                valgrind \
                gdb \
                lcov
            ;;
        "zypper")
            # Update package database
            sudo zypper refresh
            
            # Install build tools
            sudo zypper install -y \
                gcc \
                gcc-c++ \
                make \
                cmake \
                pkg-config \
                git \
                wget \
                curl
            
            # Install development libraries
            sudo zypper install -y \
                libopenssl-devel \
                jsoncpp-devel
            
            # Install optional development tools
            sudo zypper install -y \
                clang \
                cppcheck \
                valgrind \
                gdb \
                lcov
            ;;
        *)
            print_error "Unsupported package manager: $PACKAGE_MANAGER"
            exit 1
            ;;
    esac
    
    print_success "Dependencies installed successfully"
}

# Function to clean build directory
clean_build() {
    if [ "$CLEAN" = "true" ]; then
        print_status "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        rm -rf "$DIST_DIR"
        print_success "Build directory cleaned"
    fi
}

# Function to build project
build_project() {
    print_status "Building simple-nfsd..."
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure CMake
    cmake .. \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_SHARED_LIBS="$BUILD_SHARED_LIBS" \
        -DENABLE_TESTS="$BUILD_TESTS" \
        -DENABLE_SSL="$ENABLE_SSL" \
        -DENABLE_JSON="$ENABLE_JSON" \
        -DENABLE_STATIC_LINKING="$ENABLE_STATIC_LINKING" \
        -DENABLE_PACKAGING=ON
    
    # Build project
    make -j$(nproc)
    
    print_success "Build completed successfully"
}

# Function to run tests
run_tests() {
    if [ "$BUILD_TESTS" = "ON" ]; then
        print_status "Running tests..."
        cd "$BUILD_DIR"
        
        if make test; then
            print_success "All tests passed"
        else
            print_warning "Some tests failed, but continuing..."
        fi
    fi
}

# Function to install project
install_project() {
    if [ "$INSTALL" = "true" ]; then
        print_status "Installing simple-nfsd..."
        cd "$BUILD_DIR"
        
        sudo make install
        
        # Test installation
        if command_exists simple-nfsd; then
            print_success "Installation successful"
            simple-nfsd --version
        else
            print_error "Installation failed - binary not found in PATH"
            exit 1
        fi
    fi
}

# Function to create packages
create_packages() {
    if [ "$PACKAGE" = "true" ]; then
        print_status "Creating packages..."
        cd "$BUILD_DIR"
        
        # Create distribution directory
        mkdir -p "$DIST_DIR"
        
        # Create packages using CPack
        cpack
        
        # Move packages to dist directory
        mv *.deb *.rpm *.tar.gz "$DIST_DIR/" 2>/dev/null || true
        
        print_success "Packages created in $DIST_DIR"
        ls -la "$DIST_DIR"
    fi
}

# Function to create system service
create_service() {
    if [ "$SERVICE" = "true" ]; then
        print_status "Creating systemd service..."
        
        # Create systemd service file
        sudo tee /etc/systemd/system/simple-nfsd.service > /dev/null << EOF
[Unit]
Description=Simple NFS Daemon - A lightweight and secure NFS server
After=network.target

[Service]
Type=simple
User=simple-nfsd
Group=simple-nfsd
ExecStart=/usr/local/bin/simple-nfsd --daemon
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
        
        # Create user and group
        sudo useradd --system --no-create-home --shell /bin/false simple-nfsd 2>/dev/null || true
        
        # Reload systemd and enable service
        sudo systemctl daemon-reload
        sudo systemctl enable simple-nfsd
        
        print_success "Systemd service created and enabled"
        print_status "Use 'sudo systemctl start simple-nfsd' to start the service"
    fi
}

# Function to show usage
show_usage() {
    cat << EOF
simple-nfsd - Linux Build Script

Usage: $0 [OPTIONS]

Options:
    -h, --help              Show this help message
    -d, --deps              Install dependencies only
    -b, --build             Build project only
    -t, --test              Run tests only
    -i, --install           Install project only
    -p, --package           Create packages only
    -s, --service           Create system service
    -a, --all               Full build and install (default)
    --static                Build static binary
    --debug                 Build in debug mode
    --clean                 Clean build directory before building
    --no-tests              Disable tests
    --no-ssl                Disable SSL support
    --no-json               Disable JSON support

Examples:
    $0                      # Full build and install
    $0 --deps               # Install dependencies only
    $0 --build --test       # Build and test
    $0 --static --package   # Build static binary and create packages
    $0 --clean --all        # Clean build and full install

EOF
}

# Function to parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -d|--deps)
                DEPENDENCIES_ONLY=true
                shift
                ;;
            -b|--build)
                BUILD_ONLY=true
                shift
                ;;
            -t|--test)
                TEST_ONLY=true
                shift
                ;;
            -i|--install)
                INSTALL_ONLY=true
                shift
                ;;
            -p|--package)
                PACKAGE_ONLY=true
                shift
                ;;
            -s|--service)
                SERVICE_ONLY=true
                shift
                ;;
            -a|--all)
                ALL=true
                shift
                ;;
            --static)
                ENABLE_STATIC_LINKING="ON"
                shift
                ;;
            --debug)
                BUILD_TYPE="Debug"
                shift
                ;;
            --clean)
                CLEAN="true"
                shift
                ;;
            --no-tests)
                BUILD_TESTS="OFF"
                shift
                ;;
            --no-ssl)
                ENABLE_SSL="OFF"
                shift
                ;;
            --no-json)
                ENABLE_JSON="OFF"
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Set default action if none specified
    if [ -z "$DEPENDENCIES_ONLY" ] && [ -z "$BUILD_ONLY" ] && [ -z "$TEST_ONLY" ] && 
       [ -z "$INSTALL_ONLY" ] && [ -z "$PACKAGE_ONLY" ] && [ -z "$SERVICE_ONLY" ] && 
       [ -z "$ALL" ]; then
        ALL=true
    fi
}

# Main execution
main() {
    print_status "Starting simple-nfsd build process..."
    
    # Parse command line arguments
    parse_arguments "$@"
    
    # Detect distribution
    detect_distro
    
    # Install dependencies
    if [ "$DEPENDENCIES_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        install_dependencies
        if [ "$DEPENDENCIES_ONLY" = "true" ]; then
            print_success "Dependencies installed successfully"
            exit 0
        fi
    fi
    
    # Clean build directory
    clean_build
    
    # Build project
    if [ "$BUILD_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        build_project
        if [ "$BUILD_ONLY" = "true" ]; then
            print_success "Build completed successfully"
            exit 0
        fi
    fi
    
    # Run tests
    if [ "$TEST_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        run_tests
        if [ "$TEST_ONLY" = "true" ]; then
            print_success "Tests completed"
            exit 0
        fi
    fi
    
    # Install project
    if [ "$INSTALL_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        INSTALL="true"
        install_project
        if [ "$INSTALL_ONLY" = "true" ]; then
            print_success "Installation completed successfully"
            exit 0
        fi
    fi
    
    # Create packages
    if [ "$PACKAGE_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        PACKAGE="true"
        create_packages
        if [ "$PACKAGE_ONLY" = "true" ]; then
            print_success "Packages created successfully"
            exit 0
        fi
    fi
    
    # Create service
    if [ "$SERVICE_ONLY" = "true" ] || [ "$ALL" = "true" ]; then
        SERVICE="true"
        create_service
        if [ "$SERVICE_ONLY" = "true" ]; then
            print_success "Service created successfully"
            exit 0
        fi
    fi
    
    print_success "simple-nfsd build process completed successfully!"
}

# Run main function with all arguments
main "$@"
