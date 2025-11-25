#!/bin/bash

# simple-nfsd macOS Build Script
# This script builds the simple-nfsd application for macOS

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
VERSION="0.1.0"

# Build options
BUILD_TYPE="Release"
BUILD_SHARED_LIBS="OFF"
BUILD_TESTS="ON"
ENABLE_SSL="ON"
ENABLE_JSON="ON"
ENABLE_STATIC_LINKING="OFF"
PACKAGE="false"
INSTALL="false"
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

# Function to check macOS version
check_macos_version() {
    print_status "Checking macOS version..."
    
    # Get macOS version
    MACOS_VERSION=$(sw_vers -productVersion)
    MACOS_MAJOR=$(echo "$MACOS_VERSION" | cut -d. -f1)
    MACOS_MINOR=$(echo "$MACOS_VERSION" | cut -d. -f2)
    
    print_status "macOS version: $MACOS_VERSION"
    
    # Check if running on macOS 10.15 or later
    if [ "$MACOS_MAJOR" -lt 10 ] || ([ "$MACOS_MAJOR" -eq 10 ] && [ "$MACOS_MINOR" -lt 15 ]); then
        print_warning "macOS 10.15 or later recommended for best compatibility"
    fi
}

# Function to check Xcode Command Line Tools
check_xcode_tools() {
    print_status "Checking Xcode Command Line Tools..."
    
    if ! command_exists xcode-select; then
        print_error "Xcode Command Line Tools not found"
        print_status "Please install Xcode Command Line Tools:"
        print_status "xcode-select --install"
        exit 1
    fi
    
    # Check if Xcode Command Line Tools are installed
    if ! xcode-select -p >/dev/null 2>&1; then
        print_error "Xcode Command Line Tools not properly installed"
        print_status "Please install Xcode Command Line Tools:"
        print_status "xcode-select --install"
        exit 1
    fi
    
    print_success "Xcode Command Line Tools found"
}

# Function to check Homebrew
check_homebrew() {
    print_status "Checking Homebrew installation..."
    
    if ! command_exists brew; then
        print_error "Homebrew not found"
        print_status "Please install Homebrew from https://brew.sh/"
        exit 1
    fi
    
    print_success "Homebrew found: $(brew --version | head -n1)"
}

# Function to install dependencies
install_dependencies() {
    print_status "Installing build dependencies using Homebrew..."
    
    # Update Homebrew
    brew update
    
    # Install build tools
    brew install cmake pkg-config git wget curl
    
    # Install development libraries
    brew install openssl jsoncpp
    
    # Install optional development tools
    brew install clang-format cppcheck valgrind gdb lcov
    
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
    make -j$(sysctl -n hw.ncpu)
    
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
        
        make install
        
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
        print_status "Creating macOS packages..."
        cd "$BUILD_DIR"
        
        # Create distribution directory
        mkdir -p "$DIST_DIR"
        
        # Create packages using CPack
        cpack
        
        # Move packages to dist directory
        mv *.dmg *.pkg "$DIST_DIR/" 2>/dev/null || true
        
        print_success "Packages created in $DIST_DIR"
        ls -la "$DIST_DIR"
    fi
}

# Function to create static binary package
create_static_package() {
    if [ "$ENABLE_STATIC_LINKING" = "ON" ]; then
        print_status "Creating static binary package..."
        
        # Create distribution directory
        mkdir -p "$DIST_DIR"
        
        # Create static binary directory
        STATIC_DIR="$DIST_DIR/simple-nfsd-$VERSION-static-macos"
        mkdir -p "$STATIC_DIR"
        
        # Copy binary and files
        cp "$BUILD_DIR/simple-nfsd" "$STATIC_DIR/"
        cp "$PROJECT_ROOT/README.md" "$STATIC_DIR/"
        cp "$PROJECT_ROOT/LICENSE" "$STATIC_DIR/"
        cp -r "$PROJECT_ROOT/config" "$STATIC_DIR/" 2>/dev/null || true
        
        # Create tarball
        cd "$DIST_DIR"
        tar -czf "simple-nfsd-$VERSION-static-macos.tar.gz" "simple-nfsd-$VERSION-static-macos"
        rm -rf "simple-nfsd-$VERSION-static-macos"
        
        print_success "Static binary package created: simple-nfsd-$VERSION-static-macos.tar.gz"
    fi
}

# Function to show usage
show_usage() {
    cat << EOF
simple-nfsd - macOS Build Script

Usage: $0 [OPTIONS]

Options:
    -h, --help              Show this help message
    -d, --deps              Install dependencies only
    -b, --build             Build project only
    -t, --test              Run tests only
    -i, --install           Install project only
    -p, --package           Create packages only
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
       [ -z "$INSTALL_ONLY" ] && [ -z "$PACKAGE_ONLY" ] && [ -z "$ALL" ]; then
        ALL=true
    fi
}

# Main execution
main() {
    print_status "Starting simple-nfsd build process..."
    
    # Parse command line arguments
    parse_arguments "$@"
    
    # Check system requirements
    check_macos_version
    check_xcode_tools
    check_homebrew
    
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
        create_static_package
        if [ "$PACKAGE_ONLY" = "true" ]; then
            print_success "Packages created successfully"
            exit 0
        fi
    fi
    
    print_success "simple-nfsd build process completed successfully!"
}

# Run main function with all arguments
main "$@"
