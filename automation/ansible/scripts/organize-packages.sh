#!/bin/bash
# Organize packages script for simple-nfsd
# Moves recently built packages to dist/centralized/v{VERSION}/ directory
# This script should be run on remote build VMs after packages are created

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
VERSION=$(grep '^VERSION =' "$PROJECT_ROOT/Makefile" 2>/dev/null | cut -d' ' -f3 || echo "0.2.0")
DIST_DIR="$PROJECT_ROOT/dist"
BUILD_DIR="$PROJECT_ROOT/build"
CENTRAL_RELEASE_DIR="$DIST_DIR/centralized"
VERSION_DIR="$CENTRAL_RELEASE_DIR/v$VERSION"

# Functions
print_header() {
    echo -e "${BLUE}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║              Organize Packages Script for simple-nfsd v$VERSION       ║${NC}"
    echo -e "${BLUE}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_info() {
    echo -e "${GREEN}ℹ️  $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_step() {
    echo -e "${CYAN}🔧 $1${NC}"
}

print_package() {
    echo -e "${PURPLE}📦 $1${NC}"
}

check_directories() {
    print_step "Checking directories..."
    
    if [[ ! -d "$PROJECT_ROOT" ]]; then
        print_error "Project root not found: $PROJECT_ROOT"
        exit 1
    fi
    
    if [[ ! -d "$DIST_DIR" ]] && [[ ! -d "$BUILD_DIR" ]]; then
        print_error "Neither dist/ nor build/ directory found"
        print_info "Expected: $DIST_DIR or $BUILD_DIR"
        exit 1
    fi
    
    print_success "Directories checked"
}

create_centralized_structure() {
    print_step "Creating centralized release directory structure..."
    
    if [[ ! -d "$CENTRAL_RELEASE_DIR" ]]; then
        mkdir -p "$CENTRAL_RELEASE_DIR"
        print_info "Created: $CENTRAL_RELEASE_DIR"
    fi
    
    if [[ ! -d "$VERSION_DIR" ]]; then
        mkdir -p "$VERSION_DIR"
        print_info "Created: $VERSION_DIR"
    fi
    
    print_success "Directory structure ready"
}

move_packages() {
    print_step "Moving packages to centralized directory..."
    
    local moved=0
    local skipped=0
    
    # Package patterns to look for
    local patterns=(
        "*.deb"
        "*.rpm"
        "*.dmg"
        "*.pkg"
        "*.tar.gz"
        "*.zip"
        "*-src.tar.gz"
        "*-src.zip"
    )
    
    # Search in dist/ directory first, then build/ as fallback
    local search_dirs=("$DIST_DIR" "$BUILD_DIR")
    
    for search_dir in "${search_dirs[@]}"; do
        if [[ ! -d "$search_dir" ]]; then
            continue
        fi
        
        print_info "Searching in: $search_dir"
        
        for pattern in "${patterns[@]}"; do
            # Skip source packages that are already in centralized (they're created once)
            if [[ "$pattern" == *"-src"* ]] && [[ -f "$VERSION_DIR"/*-src.tar.gz ]] || [[ -f "$VERSION_DIR"/*-src.zip ]]; then
                continue
            fi
            
            while IFS= read -r -d '' file; do
                if [[ -f "$file" ]]; then
                    local filename=$(basename "$file")
                    local dest="$VERSION_DIR/$filename"
                    
                    # Skip if already exists and is newer or same age
                    if [[ -f "$dest" ]]; then
                        if [[ "$file" -nt "$dest" ]]; then
                            mv "$file" "$dest"
                            print_package "Moved (updated): $filename"
                            ((moved++))
                        else
                            print_info "Skipped (already exists): $filename"
                            ((skipped++))
                        fi
                    else
                        mv "$file" "$dest"
                        print_package "Moved: $filename"
                        ((moved++))
                    fi
                fi
            done < <(find "$search_dir" -maxdepth 1 -type f -name "$pattern" -print0 2>/dev/null)
        done
    done
    
    if [[ $moved -gt 0 ]]; then
        print_success "Moved $moved package(s) to $VERSION_DIR"
    fi
    
    if [[ $skipped -gt 0 ]]; then
        print_info "Skipped $skipped package(s) (already exist)"
    fi
    
    if [[ $moved -eq 0 && $skipped -eq 0 ]]; then
        print_warning "No packages found to move"
        return 1
    fi
    
    return 0
}

list_packages() {
    print_step "Listing packages in centralized directory..."
    
    if [[ ! -d "$VERSION_DIR" ]]; then
        print_warning "Version directory not found: $VERSION_DIR"
        return 1
    fi
    
    local count=0
    while IFS= read -r -d '' file; do
        if [[ -f "$file" ]]; then
            local filename=$(basename "$file")
            local size=$(du -h "$file" | cut -f1)
            print_package "$filename ($size)"
            ((count++))
        fi
    done < <(find "$VERSION_DIR" -maxdepth 1 -type f \( -name "*.deb" -o -name "*.rpm" -o -name "*.tar.gz" -o -name "*.zip" -o -name "*.dmg" -o -name "*.pkg" -o -name "*.exe" -o -name "*.msi" \) -print0 2>/dev/null)
    
    if [[ $count -eq 0 ]]; then
        print_warning "No packages found in $VERSION_DIR"
        return 1
    else
        print_success "Found $count package(s)"
    fi
    
    return 0
}

show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help          Show this help message"
    echo "  -l, --list          List packages in centralized directory"
    echo "  -v, --version        Show version information"
    echo ""
    echo "This script moves recently built packages from dist/ and build/ directories"
    echo "to the centralized release directory: $CENTRAL_RELEASE_DIR/v$VERSION/"
    echo ""
    echo "Package types moved:"
    echo "  - Linux: .deb, .rpm"
    echo "  - macOS: .dmg, .pkg"
    echo "  - Windows: .exe, .msi"
    echo "  - Archives: .tar.gz, .zip"
    echo "  - Source: *-src.tar.gz, *-src.zip"
}

main() {
    print_header
    
    # Parse command line arguments
    local do_list=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -l|--list)
                do_list=true
                shift
                ;;
            -v|--version)
                echo "Organize Packages Script v$VERSION"
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    # Check prerequisites
    check_directories
    
    # Create directory structure
    create_centralized_structure
    
    # List packages if requested
    if [[ "$do_list" == true ]]; then
        list_packages
        exit 0
    fi
    
    # Move packages
    if move_packages; then
        echo ""
        list_packages
        echo ""
        print_success "Packages organized successfully!"
        print_info "Packages are now in: $VERSION_DIR"
    else
        print_warning "No packages were moved"
        exit 1
    fi
}

# Run main function with all arguments
main "$@"
