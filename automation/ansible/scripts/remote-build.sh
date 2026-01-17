#!/bin/bash

# Remote Build Script for simple-nfsd
# Builds the application on remote VMware Fusion VMs using Ansible

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANSIBLE_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$(dirname "$ANSIBLE_DIR")")"

# Default values
INVENTORY_FILE="${ANSIBLE_DIR}/inventory-vmware.ini"
PLAYBOOK="${ANSIBLE_DIR}/playbook-build.yml"
GIT_BRANCH="main"
BUILD_TYPE="Release"
CLEAN_BUILD=false
RUN_TESTS=true
CREATE_PACKAGES=false
LIMIT_HOSTS=""
ASK_BECOME_PASS=false

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

# Function to show usage
show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build simple-nfsd on remote VMware Fusion VMs using Ansible.

OPTIONS:
    -h, --help              Show this help message
    -i, --inventory FILE    Path to Ansible inventory file (default: inventory-vmware.ini)
    -b, --branch BRANCH     Git branch to build (default: main)
    -t, --type TYPE         Build type: Release or Debug (default: Release)
    -c, --clean             Clean build directory before building
    -l, --limit HOSTS       Limit to specific hosts (comma-separated, e.g., vm1,vm2)
    --no-tests              Skip running tests
    --packages              Create packages after build
    --ask-become-pass       Prompt for sudo password
    --list-hosts            List available hosts and exit

EXAMPLES:
    $0                                    # Build on all VMs
    $0 -l BUILD_DEB                       # Build only on BUILD_DEB
    $0 -b develop -t Debug                 # Build develop branch in Debug mode
    $0 -c --packages                       # Clean build and create packages
    $0 --list-hosts                       # List available hosts

EOF
}

# Function to check prerequisites
check_prerequisites() {
    print_status "Checking prerequisites..."
    
    if ! command -v ansible-playbook &> /dev/null; then
        print_error "ansible-playbook is not installed"
        print_status "Install it with: pip3 install ansible"
        exit 1
    fi
    
    if [ ! -f "$INVENTORY_FILE" ]; then
        print_error "Inventory file not found: $INVENTORY_FILE"
        print_status "Please create the inventory file or specify one with -i"
        exit 1
    fi
    
    if [ ! -f "$PLAYBOOK" ]; then
        print_error "Playbook not found: $PLAYBOOK"
        exit 1
    fi
    
    print_success "Prerequisites check passed"
}

# Function to list hosts
list_hosts() {
    print_status "Available hosts:"
    ansible-inventory -i "$INVENTORY_FILE" --list | grep -A 10 '"build-vms"' || true
    ansible all -i "$INVENTORY_FILE" --list-hosts 2>/dev/null || true
}

# Function to test connectivity
test_connectivity() {
    print_status "Testing connectivity to VMs..."
    
    if [ -n "$LIMIT_HOSTS" ]; then
        ansible "$LIMIT_HOSTS" -i "$INVENTORY_FILE" -m ping
    else
        ansible build-vms -i "$INVENTORY_FILE" -m ping
    fi
    
    if [ $? -eq 0 ]; then
        print_success "Connectivity test passed"
    else
        print_error "Connectivity test failed"
        print_warning "Please check:"
        print_warning "  1. VM IP addresses in inventory file"
        print_warning "  2. SSH key authentication is set up"
        print_warning "  3. VMs are running and accessible"
        exit 1
    fi
}

# Function to run the build
run_build() {
    print_status "Starting remote build..."
    print_status "Inventory: $INVENTORY_FILE"
    print_status "Playbook: $PLAYBOOK"
    print_status "Branch: $GIT_BRANCH"
    print_status "Build type: $BUILD_TYPE"
    print_status "Clean build: $CLEAN_BUILD"
    print_status "Run tests: $RUN_TESTS"
    print_status "Create packages: $CREATE_PACKAGES"
    print_status "Ask become pass: $ASK_BECOME_PASS"
    
    # Build ansible-playbook command
    ANSIBLE_CMD="ansible-playbook -i $INVENTORY_FILE $PLAYBOOK"
    
    # Add limit if specified
    if [ -n "$LIMIT_HOSTS" ]; then
        ANSIBLE_CMD="$ANSIBLE_CMD --limit $LIMIT_HOSTS"
    fi
    
    # Add extra variables
    ANSIBLE_CMD="$ANSIBLE_CMD -e git_branch=$GIT_BRANCH"
    ANSIBLE_CMD="$ANSIBLE_CMD -e build_type=$BUILD_TYPE"
    ANSIBLE_CMD="$ANSIBLE_CMD -e clean_build=$CLEAN_BUILD"
    ANSIBLE_CMD="$ANSIBLE_CMD -e run_tests=$RUN_TESTS"
    ANSIBLE_CMD="$ANSIBLE_CMD -e create_packages=$CREATE_PACKAGES"
    
    # Add --ask-become-pass if requested
    if [ "$ASK_BECOME_PASS" = true ]; then
      ANSIBLE_CMD="$ANSIBLE_CMD --ask-become-pass"
    fi
    
    print_status "Running: $ANSIBLE_CMD"
    echo ""
    
    # Execute the command
    eval $ANSIBLE_CMD
    
    if [ $? -eq 0 ]; then
        print_success "Build completed successfully!"
    else
        print_error "Build failed!"
        exit 1
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -i|--inventory)
            INVENTORY_FILE="$2"
            shift 2
            ;;
        -b|--branch)
            GIT_BRANCH="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -l|--limit)
            LIMIT_HOSTS="$2"
            shift 2
            ;;
        --no-tests)
            RUN_TESTS=false
            shift
            ;;
        --packages)
            CREATE_PACKAGES=true
            shift
            ;;
        --ask-become-pass)
            ASK_BECOME_PASS=true
            shift
            ;;
        --list-hosts)
            check_prerequisites
            list_hosts
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Main execution
main() {
    print_status "=== Remote Build Script for simple-nfsd ==="
    echo ""
    
    check_prerequisites
    echo ""
    
    test_connectivity
    echo ""
    
    run_build
}

# Run main function
main "$@"
