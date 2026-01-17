#!/bin/bash
# Collect packages from remote VMs to local dist directory
# This script fetches all built packages (DEB, RPM, TGZ, ZIP) from the Ansible VMs
# and organizes them in a local dist directory for GitHub release

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ANSIBLE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$ANSIBLE_DIR/../.." && pwd)"
DIST_DIR="$PROJECT_ROOT/dist"
INVENTORY_FILE="$ANSIBLE_DIR/inventory-vmware.ini"
PROJECT_NAME="simple-nfsd"
REMOTE_PROJECT_DIR="/opt/simple-nfsd"
REMOTE_DIST_DIR="$REMOTE_PROJECT_DIR/dist"
REMOTE_BUILD_DIR="$REMOTE_PROJECT_DIR/build"
# Get version from Makefile (assuming it's in the project root)
VERSION=$(grep '^VERSION =' "$PROJECT_ROOT/Makefile" 2>/dev/null | cut -d' ' -f3 || echo "0.2.0")
REMOTE_CENTRALIZED_DIR="$REMOTE_PROJECT_DIR/dist/centralized/v$VERSION"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print functions
print_info() {
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

# Check prerequisites
check_prerequisites() {
    print_info "Checking prerequisites..."
    
    if ! command -v ansible &> /dev/null; then
        print_error "ansible is not installed. Install with: pip3 install ansible"
        exit 1
    fi
    
    if [ ! -f "$INVENTORY_FILE" ]; then
        print_error "Inventory file not found: $INVENTORY_FILE"
        exit 1
    fi
    
    print_success "Prerequisites check passed"
}

# Create local dist directory structure
create_dist_structure() {
    print_info "Creating local dist directory structure..."
    
    mkdir -p "$DIST_DIR"/{linux,source,macos}
    mkdir -p "$DIST_DIR/linux"/{deb,rpm}
    mkdir -p "$DIST_DIR/macos"/{dmg,pkg}
    
    print_success "Dist directory structure created: $DIST_DIR"
}

# Fetch packages from remote VMs
fetch_packages() {
    print_info "Fetching packages from remote VMs..."
    
    # Ensure dist directory structure exists before Ansible runs
    mkdir -p "$DIST_DIR/linux"/{deb,rpm}
    mkdir -p "$DIST_DIR/macos"/{dmg,pkg}
    mkdir -p "$DIST_DIR/source"
    
    # Create a temporary playbook for fetching packages
    TEMP_PLAYBOOK=$(mktemp)
    cat > "$TEMP_PLAYBOOK" << PLAYBOOK_EOF
---
- name: Collect packages from remote VMs
  hosts: build-vms
  gather_facts: no
  vars:
    project_dir: "/opt/simple-nfsd"
    remote_dist_dir: "/opt/simple-nfsd/dist"
    remote_build_dir: "/opt/simple-nfsd/build"
    remote_centralized_dir: "/opt/simple-nfsd/dist/centralized/v$VERSION"
    local_dist_dir: "$DIST_DIR"
    
  tasks:
    - name: Check if dist directory exists on remote
      stat:
        path: "{{ remote_dist_dir }}"
      register: dist_dir_stat
      
    - name: Check if build directory exists on remote
      stat:
        path: "{{ remote_build_dir }}"
      register: build_dir_stat
      
    - name: Check if centralized directory exists on remote
      stat:
        path: "{{ remote_centralized_dir }}"
      register: centralized_dir_stat
      
    - name: List packages in centralized directory (preferred)
      find:
        paths: "{{ remote_centralized_dir }}"
        patterns:
          - "*.deb"
          - "*.rpm"
          - "*.tar.gz"
          - "*.zip"
          - "*.dmg"
          - "*.pkg"
          - "*-src.tar.gz"
          - "*-src.zip"
        excludes: "*.tar.Z"
      register: centralized_packages
      when: centralized_dir_stat.stat.exists
      
    - name: List packages in dist directory
      find:
        paths: "{{ remote_dist_dir }}"
        patterns:
          - "*.deb"
          - "*.rpm"
          - "*.dmg"
          - "*.pkg"
          - "*-src.tar.gz"
          - "*-src.zip"
        excludes: "*.tar.Z"
      register: dist_packages
      when: dist_dir_stat.stat.exists
      
    - name: List packages in build directory (fallback)
      find:
        paths: "{{ remote_build_dir }}"
        patterns:
          - "*.deb"
          - "*.rpm"
          - "*.dmg"
          - "*.pkg"
          - "*-src.tar.gz"
          - "*-src.zip"
        excludes: "*.tar.Z"
      register: build_packages
      when: build_dir_stat.stat.exists
      
    - name: Fetch DEB packages from centralized (preferred)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/deb/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ centralized_packages.files | default([]) }}"
      when: 
        - centralized_packages.files is defined
        - (item.path | regex_search('\.deb$')) is not none
          
    - name: Fetch DEB packages from dist
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/deb/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ dist_packages.files | default([]) }}"
      when: 
        - dist_packages.files is defined
        - (item.path | regex_search('\.deb$')) is not none
        - (centralized_packages.files | default([]) | length) == 0
          
    - name: Fetch RPM packages from centralized (preferred)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/rpm/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ centralized_packages.files | default([]) }}"
      when: 
        - centralized_packages.files is defined
        - (item.path | regex_search('\.rpm$')) is not none
          
    - name: Fetch RPM packages from dist
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/rpm/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ dist_packages.files | default([]) }}"
      when: 
        - dist_packages.files is defined
        - (item.path | regex_search('\.rpm$')) is not none
        - (centralized_packages.files | default([]) | length) == 0
          
    - name: Fetch DMG packages from centralized (preferred)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/dmg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ centralized_packages.files | default([]) }}"
      when: 
        - centralized_packages.files is defined
        - (item.path | regex_search('\.dmg$')) is not none
          
    - name: Fetch DMG packages from dist
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/dmg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ dist_packages.files | default([]) }}"
      when: 
        - dist_packages.files is defined
        - (item.path | regex_search('\.dmg$')) is not none
        - (centralized_packages.files | default([]) | length) == 0
          
    - name: Fetch PKG packages from centralized (preferred)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/pkg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ centralized_packages.files | default([]) }}"
      when: 
        - centralized_packages.files is defined
        - (item.path | regex_search('\.pkg$')) is not none
          
    - name: Fetch PKG packages from dist
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/pkg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ dist_packages.files | default([]) }}"
      when: 
        - dist_packages.files is defined
        - (item.path | regex_search('\.pkg$')) is not none
        - (centralized_packages.files | default([]) | length) == 0
          
    - name: Fetch source packages from centralized (preferred)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/source/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ centralized_packages.files | default([]) }}"
      when: 
        - centralized_packages.files is defined
        - (item.path | regex_search('-src\.(tar\.gz|zip)$')) is not none
        - (item.path | regex_search('\.(deb|rpm|dmg|pkg)$')) is none
          
    - name: Fetch source packages from dist
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/source/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ dist_packages.files | default([]) }}"
      when: 
        - dist_packages.files is defined
        - (item.path | regex_search('-src\.(tar\.gz|zip)$')) is not none
        - (item.path | regex_search('\.(deb|rpm|dmg|pkg)$')) is none
        - (centralized_packages.files | default([]) | length) == 0
        
    - name: Fetch DEB packages from build (fallback)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/deb/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ build_packages.files | default([]) }}"
      when: 
        - build_packages.files is defined
        - (item.path | regex_search('\.deb$')) is not none
        - (dist_packages.files | default([]) | length) == 0
        
    - name: Fetch RPM packages from build (fallback)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/linux/rpm/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ build_packages.files | default([]) }}"
      when: 
        - build_packages.files is defined
        - (item.path | regex_search('\.rpm$')) is not none
        - (dist_packages.files | default([]) | length) == 0
        
    - name: Fetch DMG packages from build (fallback)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/dmg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ build_packages.files | default([]) }}"
      when: 
        - build_packages.files is defined
        - (item.path | regex_search('\.dmg$')) is not none
        - (dist_packages.files | default([]) | length) == 0
        
    - name: Fetch PKG packages from build (fallback)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/macos/pkg/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ build_packages.files | default([]) }}"
      when: 
        - build_packages.files is defined
        - (item.path | regex_search('\.pkg$')) is not none
        - (dist_packages.files | default([]) | length) == 0
        
    - name: Fetch source packages from build (fallback)
      fetch:
        src: "{{ item.path }}"
        dest: "{{ local_dist_dir }}/source/{{ inventory_hostname }}-{{ item.path | basename }}"
        flat: yes
      loop: "{{ build_packages.files | default([]) }}"
      when: 
        - build_packages.files is defined
        - (item.path | regex_search('-src\.(tar\.gz|zip)$')) is not none
        - (item.path | regex_search('\.(deb|rpm|dmg|pkg)$')) is none
        - (dist_packages.files | default([]) | length) == 0
PLAYBOOK_EOF

    # Run the playbook
    if ansible-playbook -i "$INVENTORY_FILE" "$TEMP_PLAYBOOK"; then
        print_success "Packages fetched successfully"
    else
        print_error "Failed to fetch packages"
        rm -f "$TEMP_PLAYBOOK"
        exit 1
    fi
    
    # Clean up
    rm -f "$TEMP_PLAYBOOK"
}

# Organize and rename packages
organize_packages() {
    print_info "Organizing packages..."
    
    # Get version from Makefile
    VERSION=$(grep '^VERSION =' "$PROJECT_ROOT/Makefile" 2>/dev/null | cut -d' ' -f3 || echo "0.2.0")
    CENTRALIZED_DIR="$DIST_DIR/centralized/v$VERSION"
    
    # Create centralized directory structure
    mkdir -p "$CENTRALIZED_DIR"
    print_info "Created centralized directory: $CENTRALIZED_DIR"
    
    # Remove hostname prefix from package names and copy to centralized directory
    local copied=0
    for dir in "$DIST_DIR/linux"/{deb,rpm} "$DIST_DIR/macos"/{dmg,pkg} "$DIST_DIR/source"; do
        if [ -d "$dir" ]; then
            cd "$dir"
            shopt -s nullglob
            for file in *; do
                if [ -f "$file" ]; then
                    # Remove hostname prefix if present
                    if [[ "$file" =~ ^(BUILD_DEB|BUILD_RPM|BUILD_MACOS)- ]]; then
                        new_name="${file#BUILD_DEB-}"
                        new_name="${new_name#BUILD_RPM-}"
                        new_name="${new_name#BUILD_MACOS-}"
                        if [ "$file" != "$new_name" ]; then
                            mv "$file" "$new_name"
                            print_info "Renamed: $file -> $new_name"
                            file="$new_name"
                        fi
                    fi
                    
                    # Copy to centralized directory (skip if already exists and is same or newer)
                    local dest="$CENTRALIZED_DIR/$file"
                    if [ ! -f "$dest" ] || [ "$file" -nt "$dest" ]; then
                        cp "$file" "$dest"
                        print_info "Copied to centralized: $file"
                        ((copied++))
                    fi
                fi
            done
            shopt -u nullglob
        fi
    done
    
    # Create symlinks or copies in main directories (for backward compatibility)
    cd "$DIST_DIR/linux"
    shopt -s nullglob
    for file in deb/*.deb rpm/*.rpm; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            if [ ! -f "$filename" ]; then
                ln -s "${file#linux/}" "$filename" 2>/dev/null || cp "$file" "$filename"
            fi
        fi
    done
    shopt -u nullglob
    
    cd "$DIST_DIR/macos"
    shopt -s nullglob
    for file in dmg/*.dmg pkg/*.pkg; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            if [ ! -f "$filename" ]; then
                ln -s "${file#macos/}" "$filename" 2>/dev/null || cp "$file" "$filename"
            fi
        fi
    done
    shopt -u nullglob
    
    if [ $copied -gt 0 ]; then
        print_success "Packages organized: $copied package(s) copied to $CENTRALIZED_DIR"
    else
        print_info "Packages organized (no new packages to copy)"
    fi
}

# Display summary
display_summary() {
    print_info "Package collection summary:"
    echo ""
    
    local total=0
    local count=0
    
    echo "DEB packages:"
    if ls "$DIST_DIR/linux/deb"/*.deb 1> /dev/null 2>&1; then
        for file in "$DIST_DIR/linux/deb"/*.deb; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
                total=$((total + size))
                count=$((count + 1))
            fi
        done
    else
        echo "  (none)"
    fi
    
    echo ""
    echo "RPM packages:"
    if ls "$DIST_DIR/linux/rpm"/*.rpm 1> /dev/null 2>&1; then
        for file in "$DIST_DIR/linux/rpm"/*.rpm; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
                total=$((total + size))
                count=$((count + 1))
            fi
        done
    else
        echo "  (none)"
    fi
    
    echo ""
    echo "DMG packages (macOS):"
    if ls "$DIST_DIR/macos/dmg"/*.dmg 1> /dev/null 2>&1; then
        for file in "$DIST_DIR/macos/dmg"/*.dmg; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
                total=$((total + size))
                count=$((count + 1))
            fi
        done
    else
        echo "  (none)"
    fi
    
    echo ""
    echo "PKG packages (macOS):"
    if ls "$DIST_DIR/macos/pkg"/*.pkg 1> /dev/null 2>&1; then
        for file in "$DIST_DIR/macos/pkg"/*.pkg; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
                total=$((total + size))
                count=$((count + 1))
            fi
        done
    else
        echo "  (none)"
    fi
    
    echo ""
    echo "Source packages:"
    shopt -s nullglob
    source_files=("$DIST_DIR/source"/*-src.tar.gz "$DIST_DIR/source"/*-src.zip)
    shopt -u nullglob
    if [ ${#source_files[@]} -gt 0 ] && [ -f "${source_files[0]}" ]; then
        for file in "${source_files[@]}"; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
                total=$((total + size))
                count=$((count + 1))
            fi
        done
    else
        echo "  (none)"
    fi
    
    echo ""
    
    # Show centralized directory packages
    VERSION=$(grep '^VERSION =' "$PROJECT_ROOT/Makefile" 2>/dev/null | cut -d' ' -f3 || echo "0.2.0")
    CENTRALIZED_DIR="$DIST_DIR/centralized/v$VERSION"
    echo ""
    echo "Centralized directory ($CENTRALIZED_DIR):"
    if [ -d "$CENTRALIZED_DIR" ] && ls "$CENTRALIZED_DIR"/*.{deb,rpm,dmg,pkg,tar.gz,zip} 1> /dev/null 2>&1; then
        shopt -s nullglob
        for file in "$CENTRALIZED_DIR"/*.{deb,rpm,dmg,pkg,tar.gz,zip}; do
            if [ -f "$file" ]; then
                size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
                echo "  $(basename "$file") ($(numfmt --to=iec-i --suffix=B "$size" 2>/dev/null || echo "${size}B"))"
            fi
        done
        shopt -u nullglob
    else
        echo "  (none)"
    fi
    
    echo ""
    if [ $count -gt 0 ]; then
        print_success "Total: $count package(s), $(numfmt --to=iec-i --suffix=B "$total" 2>/dev/null || echo "${total}B")"
        echo ""
        print_info "Packages are ready in: $DIST_DIR"
        print_info "Centralized packages in: $CENTRALIZED_DIR"
        echo ""
        print_info "Directory structure:"
        tree -L 3 "$DIST_DIR" 2>/dev/null || find "$DIST_DIR" -type f | head -20
    else
        print_warning "No packages found. Make sure packages were built on the VMs."
    fi
}

# Main execution
main() {
    print_info "=== Package Collection Script ==="
    echo ""
    
    check_prerequisites
    create_dist_structure
    fetch_packages
    organize_packages
    display_summary
    
    print_success "Package collection completed!"
}

# Run main function
main "$@"
