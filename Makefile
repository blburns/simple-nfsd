# Makefile for simple-nfsd
# Simple NFS Daemon - A lightweight and secure NFS server
# Copyright  SimpleDaemons <info@simpledaemons.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Variables
PROJECT_NAME = simple-nfsd
VERSION = 0.2.1
BUILD_DIR = build
DIST_DIR = dist
PACKAGE_DIR = packaging

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    # Windows specific settings
    CXX = cl
    CXXFLAGS = /std:c++17 /W3 /O2 /DNDEBUG /EHsc
    LDFLAGS = ws2_32.lib crypt32.lib
    # Windows specific flags
    CXXFLAGS += /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS
    # Detect processor cores for parallel builds
    PARALLEL_JOBS = $(shell echo %NUMBER_OF_PROCESSORS%)
    # Windows install paths
    INSTALL_PREFIX = C:/Program Files/$(PROJECT_NAME)
    CONFIG_DIR = $(INSTALL_PREFIX)/etc
    # Windows file extensions
    EXE_EXT = .exe
    LIB_EXT = .lib
    DLL_EXT = .dll
    # Windows commands
    RM = del /Q
    RMDIR = rmdir /S /Q
    MKDIR = mkdir
    CP = copy
    # Check if running in Git Bash or similar
    ifeq ($(shell echo $$SHELL),/usr/bin/bash)
        # Running in Git Bash, use Unix commands
        RM = rm -rf
        RMDIR = rm -rf
        MKDIR = mkdir -p
        CP = cp -r
        # Use Unix-style paths
        INSTALL_PREFIX = /c/Program\ Files/$(PROJECT_NAME)
        CONFIG_DIR = /c/Program\ Files/$(PROJECT_NAME)/etc
    endif
else ifeq ($(UNAME_S),Darwin)
    PLATFORM = macos
    CXX = clang++
    CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG
    LDFLAGS = -lssl -lcrypto
    # macOS specific flags
    CXXFLAGS += -target x86_64-apple-macos12.0 -target arm64-apple-macos12.0
    # Detect processor cores for parallel builds
    PARALLEL_JOBS = $(shell sysctl -n hw.ncpu)
    # macOS install paths
    INSTALL_PREFIX = /usr/local
    CONFIG_DIR = $(INSTALL_PREFIX)/etc/$(PROJECT_NAME)
    # Unix file extensions
    EXE_EXT =
    LIB_EXT = .dylib
    DLL_EXT = .dylib
    # Unix commands
    RM = rm -rf
    RMDIR = rm -rf
    MKDIR = mkdir -p
    CP = cp -r
else
    PLATFORM = linux
    CXX = g++
    CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2 -DNDEBUG
    LDFLAGS = -lssl -lcrypto -lpthread
    # Linux specific flags
    PARALLEL_JOBS = $(shell nproc)
    # Linux install paths
    INSTALL_PREFIX = /usr/local
    CONFIG_DIR = /etc/$(PROJECT_NAME)
    # Unix file extensions
    EXE_EXT =
    LIB_EXT = .so
    DLL_EXT = .so
    # Unix commands
    RM = rm -rf
    RMDIR = rm -rf
    MKDIR = mkdir -p
    CP = cp -r
endif

# Directories
SRC_DIR = src
INCLUDE_DIR = include
CONFIG_DIR_SRC = config
SCRIPTS_DIR = scripts
DEPLOYMENT_DIR = deployment

# Legacy variables for compatibility
INSTALL_DIR = $(INSTALL_PREFIX)
LOG_DIR = /var/log
DATA_DIR = /var/lib/$(PROJECT_NAME)

# Default target
all: build

# Create build directory
$(BUILD_DIR)-dir:
ifeq ($(PLATFORM),windows)
	$(MKDIR) $(BUILD_DIR)
else
	$(MKDIR) $(BUILD_DIR)
endif

# Build using CMake
build: $(BUILD_DIR)-dir
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && cmake .. -G "Visual Studio 16 2019" -A x64 && cmake --build . --config Release
else
	cd $(BUILD_DIR) && cmake .. && make -j$(PARALLEL_JOBS)
endif

# Clean build
clean:
ifeq ($(PLATFORM),windows)
	$(RMDIR) $(BUILD_DIR)
	$(RMDIR) $(DIST_DIR)
else
	$(RM) $(BUILD_DIR)
	$(RM) $(DIST_DIR)
endif

# Install
install: build
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && cmake --install . --prefix "$(INSTALL_PREFIX)"
else
	cd $(BUILD_DIR) && sudo make install
endif

# Uninstall
uninstall:
ifeq ($(PLATFORM),windows)
	$(RMDIR) "$(INSTALL_PREFIX)"
else
	sudo rm -f $(INSTALL_PREFIX)/bin/$(PROJECT_NAME)
	sudo rm -f $(INSTALL_PREFIX)/lib/lib$(PROJECT_NAME).so
	sudo rm -f $(INSTALL_PREFIX)/lib/lib$(PROJECT_NAME).dylib
	sudo rm -rf $(INSTALL_PREFIX)/include/$(PROJECT_NAME)
	sudo rm -rf $(CONFIG_DIR)
endif

# Test
test: build
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && ctest --output-on-failure
else
	cd $(BUILD_DIR) && make test
endif

# Generic package target (platform-specific)
package: build
ifeq ($(PLATFORM),macos)
	@echo "Building macOS packages..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G DragNDrop
	cd $(BUILD_DIR) && cpack -G productbuild
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.dmg $(DIST_DIR)/ 2>/dev/null || true
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.pkg $(DIST_DIR)/ 2>/dev/null || true
	@echo "macOS packages created: DMG and PKG"
else ifeq ($(PLATFORM),linux)
	@echo "Building Linux packages..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G RPM
	cd $(BUILD_DIR) && cpack -G DEB
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.rpm $(DIST_DIR)/ 2>/dev/null || true
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.deb $(DIST_DIR)/ 2>/dev/null || true
	@echo "Linux packages created: RPM and DEB"
else ifeq ($(PLATFORM),windows)
	@echo "Building Windows packages..."
	@$(MKDIR) $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G WIX
	cd $(BUILD_DIR) && cpack -G ZIP
	$(CP) $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.msi $(DIST_DIR)/ 2>/dev/null || true
	$(CP) $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.zip $(DIST_DIR)/ 2>/dev/null || true
	@echo "Windows packages created: MSI and ZIP"
else
	@echo "Package generation not supported on this platform"
endif

# Development targets
dev-build: $(BUILD_DIR)-dir
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Debug && cmake --build . --config Debug
else
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(PARALLEL_JOBS)
endif

dev-test: dev-build
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && ctest --output-on-failure
else
	cd $(BUILD_DIR) && make test
endif

# Static binary targets
static-build: $(BUILD_DIR)-dir
	@echo "Building static binary..."
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC_LINKING=ON && cmake --build . --config Release
else
	cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC_LINKING=ON && make -j$(PARALLEL_JOBS)
endif

static-test: static-build
ifeq ($(PLATFORM),windows)
	cd $(BUILD_DIR) && ctest --output-on-failure
else
	cd $(BUILD_DIR) && make test
endif

# Create static binary package
static-package: static-build
	@echo "Creating static binary package..."
	@mkdir -p $(DIST_DIR)
ifeq ($(PLATFORM),windows)
	@echo "Creating Windows static binary ZIP..."
	@mkdir -p $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows
	@cp $(BUILD_DIR)/$(PROJECT_NAME).exe $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows/
	@cp README.md $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows/
	@cp LICENSE $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows/
	@cp -r config $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows/
	@cd $(DIST_DIR) && powershell -Command "Compress-Archive -Path '$(PROJECT_NAME)-$(VERSION)-static-windows' -DestinationPath '$(PROJECT_NAME)-$(VERSION)-static-windows.zip' -Force"
	@rm -rf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-windows
	@echo "Windows static binary package created: $(PROJECT_NAME)-$(VERSION)-static-windows.zip"
else ifeq ($(PLATFORM),macos)
	@echo "Creating macOS static binary TAR.GZ..."
	@mkdir -p $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos
	@cp $(BUILD_DIR)/$(PROJECT_NAME) $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos/
	@cp README.md $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos/
	@cp LICENSE $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos/
	@cp -r config $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos/
	@cd $(DIST_DIR) && tar -czf $(PROJECT_NAME)-$(VERSION)-static-macos.tar.gz $(PROJECT_NAME)-$(VERSION)-static-macos/
	@rm -rf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-macos
	@echo "macOS static binary package created: $(PROJECT_NAME)-$(VERSION)-static-macos.tar.gz"
else ifeq ($(PLATFORM),linux)
	@echo "Creating Linux static binary TAR.GZ..."
	@mkdir -p $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux
	@cp $(BUILD_DIR)/$(PROJECT_NAME) $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux/
	@cp README.md $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux/
	@cp LICENSE $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux/
	@cp -r config $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux/
	@cd $(DIST_DIR) && tar -czf $(PROJECT_NAME)-$(VERSION)-static-linux.tar.gz $(PROJECT_NAME)-$(VERSION)-static-linux/
	@rm -rf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-linux
	@echo "Linux static binary package created: $(PROJECT_NAME)-$(VERSION)-static-linux.tar.gz"
else
	@echo "Static binary package generation not supported on this platform"
endif

# Create static binary ZIP (cross-platform)
static-zip: static-build
	@echo "Creating static binary ZIP package..."
	@mkdir -p $(DIST_DIR)
	@mkdir -p $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)
	@cp $(BUILD_DIR)/$(PROJECT_NAME)$(if $(filter windows,$(PLATFORM)),.exe,) $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)/
	@cp README.md $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)/
	@cp LICENSE $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)/
	@cp -r config $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)/
ifeq ($(PLATFORM),windows)
	@cd $(DIST_DIR) && powershell -Command "Compress-Archive -Path '$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)' -DestinationPath '$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM).zip' -Force"
else
	@cd $(DIST_DIR) && zip -r $(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM).zip $(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)/
endif
	@rm -rf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM)
	@echo "Static binary ZIP package created: $(PROJECT_NAME)-$(VERSION)-static-$(PLATFORM).zip"

# Create all static binary formats
static-all: static-package static-zip
	@echo "All static binary packages created successfully"
	@echo "Static binary packages:"
	@ls -la $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-static-* 2>/dev/null || echo "No static binary packages found"

# Code formatting
format:
ifeq ($(PLATFORM),windows)
	@echo "Code formatting on Windows requires clang-format to be installed"
	@if command -v clang-format >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i; \
	else \
		echo "clang-format not found. Install it or use the build script: scripts\\build-windows.bat --deps"; \
	fi
else
	@echo "Formatting source code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i; \
		echo "Code formatting completed successfully"; \
	else \
		echo "clang-format not found. Installing development tools..."; \
		$(MAKE) dev-deps; \
		echo "Please run 'make format' again after installation"; \
		exit 1; \
	fi
endif

# Check code style
check-style:
ifeq ($(PLATFORM),windows)
	@echo "Code style checking on Windows requires clang-format to be installed"
	@if command -v clang-format >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) -name "*.cpp" -o -name "*.hpp" | xargs clang-format --dry-run --Werror; \
	else \
		echo "clang-format not found. Install it or use the build script: scripts\\build-windows.bat --deps"; \
	fi
else
	@echo "Checking code style..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find $(SRC_DIR) $(INCLUDE_DIR) -name "*.cpp" -o -name "*.hpp" | xargs clang-format --dry-run --Werror; \
		echo "Code style check completed successfully"; \
	else \
		echo "clang-format not found. Installing development tools..."; \
		$(MAKE) dev-deps; \
		echo "Please run 'make check-style' again after installation"; \
		exit 1; \
	fi
endif

# Lint code
lint: check-style
ifeq ($(PLATFORM),windows)
	@echo "Code linting on Windows requires cppcheck to be installed"
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=all --std=c++17 $(SRC_DIR) $(INCLUDE_DIR); \
	else \
		echo "cppcheck not found. Install it or use the build script: scripts\\build-windows.bat --deps"; \
	fi
else
	@echo "Running static analysis with cppcheck..."
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=all --std=c++17 $(SRC_DIR) $(INCLUDE_DIR); \
		echo "Static analysis completed successfully"; \
	else \
		echo "cppcheck not found. Installing development tools..."; \
		$(MAKE) dev-deps; \
		echo "Please run 'make lint' again after installation"; \
		exit 1; \
	fi
endif

# Security scan
security-scan:
ifeq ($(PLATFORM),windows)
	@echo "Security scanning tools not yet implemented for Windows"
	@echo "Consider using the build script: scripts\\build-windows.bat --deps"
else
	@echo "Running security scan..."
	@if command -v bandit >/dev/null 2>&1; then \
		echo "Running bandit security scan..."; \
		bandit -r $(SRC_DIR); \
		if command -v semgrep >/dev/null 2>&1; then \
			echo "Running semgrep security scan..."; \
			semgrep --config=auto $(SRC_DIR); \
		else \
			echo "Note: semgrep not found. Skipping semgrep scan."; \
			echo "To install semgrep: pip3 install semgrep"; \
		fi; \
		echo "Security scan completed successfully"; \
	else \
		echo "Security scanning tools not found. Installing development tools..."; \
		$(MAKE) dev-deps; \
		echo "Please run 'make security-scan' again after installation"; \
		exit 1; \
	fi
endif

# Dependencies
deps:
ifeq ($(PLATFORM),macos)
	@echo "Installing dependencies on macOS..."
	sudo port install openssl jsoncpp cmake
else ifeq ($(PLATFORM),linux)
	@echo "Installing dependencies on Linux..."
	sudo apt-get update
	sudo apt-get install -y build-essential cmake libssl-dev libjsoncpp-dev
	# For RPM-based systems
	# sudo yum install -y gcc-c++ cmake openssl-devel jsoncpp-devel
else ifeq ($(PLATFORM),windows)
	@echo "Installing dependencies on Windows..."
	@echo "Please run: scripts\\build-windows.bat --deps"
	@echo "This will install vcpkg and required dependencies"
endif

# Development dependencies
dev-deps:
ifeq ($(PLATFORM),macos)
	@echo "Installing development tools on macOS..."
	@echo "Installing available packages from MacPorts..."
	sudo port install cppcheck bandit
	@echo "Note: semgrep is optional and may fail to install from MacPorts."
	@echo "You can install it manually with: pip3 install semgrep"
	@echo "Note: clang-format is not available in MacPorts."
	@echo "To install clang-format, you can:"
	@echo "  1. Use Homebrew: brew install clang-format"
	@echo "  2. Install Xcode Command Line Tools: xcode-select --install"
	@echo "  3. Install manually from LLVM releases"
else ifeq ($(PLATFORM),linux)
	@echo "Installing development tools on Linux..."
	sudo apt-get update
	sudo apt-get install -y clang-format cppcheck python3-pip
	pip3 install bandit semgrep
else ifeq ($(PLATFORM),windows)
	@echo "Installing development tools on Windows..."
	@echo "Please run: scripts\\build-windows.bat --dev-deps"
	@echo "This will install clang-format, cppcheck, and other development tools"
endif

# Docker targets
docker-build:
	docker build -t $(PROJECT_NAME):$(VERSION) .

docker-run:
	docker run -d --name $(PROJECT_NAME)-$(VERSION) -p : $(PROJECT_NAME):$(VERSION)

docker-stop:
	docker stop $(PROJECT_NAME)-$(VERSION)
	docker rm $(PROJECT_NAME)-$(VERSION)

# Service management
service-install: install
ifeq ($(PLATFORM),macos)
	@echo "Installing service on macOS..."
	@if [ -f $(DEPLOYMENT_DIR)/launchd/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo cp $(DEPLOYMENT_DIR)/launchd/com.$(PROJECT_NAME).$(PROJECT_NAME).plist /Library/LaunchDaemons/; \
		sudo launchctl load /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist; \
		echo "Service installed and started successfully"; \
	else \
		echo "Service file not found at $(DEPLOYMENT_DIR)/launchd/com.$(PROJECT_NAME).$(PROJECT_NAME).plist"; \
		echo "Please create the service file first"; \
		exit 1; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Installing service on Linux..."
	@if [ -f $(DEPLOYMENT_DIR)/systemd/$(PROJECT_NAME).service ]; then \
		sudo cp $(DEPLOYMENT_DIR)/systemd/$(PROJECT_NAME).service /etc/systemd/system/; \
		sudo systemctl daemon-reload; \
		sudo systemctl enable $(PROJECT_NAME); \
		sudo systemctl start $(PROJECT_NAME); \
		echo "Service installed and started successfully"; \
	else \
		echo "Service file not found at $(DEPLOYMENT_DIR)/systemd/$(PROJECT_NAME).service"; \
		echo "Please create the service file first"; \
		exit 1; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Installing service on Windows..."
	@if exist scripts\build-windows.bat ( \
		scripts\build-windows.bat --service; \
	) else ( \
		echo "Windows build script not found. Please install service manually:"; \
		echo "  sc create  binPath= \"$(INSTALL_PREFIX)\\bin\\$(PROJECT_NAME).exe\""; \
		echo "  sc start "; \
	)
endif

service-status:
ifeq ($(PLATFORM),macos)
	@echo "Checking service status on macOS..."
	@if launchctl list | grep -q $(PROJECT_NAME); then \
		echo "Service is running:"; \
		launchctl list | grep $(PROJECT_NAME); \
	else \
		echo "Service is not running"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Checking service status on Linux..."
	@if systemctl is-active --quiet $(PROJECT_NAME); then \
		echo "Service is running:"; \
		sudo systemctl status $(PROJECT_NAME) --no-pager -l; \
	else \
		echo "Service is not running"; \
		@if systemctl is-enabled --quiet $(PROJECT_NAME); then \
			echo "Service is enabled but not running"; \
		else \
			echo "Service is not enabled"; \
		fi; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Checking service status on Windows..."
	@sc query 
endif

# Version information
version:
	@echo "$(PROJECT_NAME) version $(VERSION)"
	@echo "Platform: $(PLATFORM)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Distribution directory: $(DIST_DIR)"
	@echo ""
	@echo "Available tags:"
	@git tag -l | sort -V | tail -10

# Help - Main help (most common targets)
help:
	@echo "Simple NFS Daemon - A lightweight and secure NFS server - Main Help"
	@echo "=================================="
	@echo ""
	@echo "Essential targets:"
	@echo "  all              - Build the project (default)"
	@echo "  build            - Build using CMake"
	@echo "  clean            - Clean build files"
	@echo "  install          - Install the project"
	@echo "  uninstall        - Uninstall the project"
	@echo "  test             - Run tests"
	@echo "  package          - Build platform-specific packages"
	@echo "  package-source   - Create source code packages (TAR.GZ + ZIP)"
	@echo "  package-all      - Create all packages (binary + source)"
	@echo "  package-info     - Show package information"
	@echo "  version          - Show version information"
	@echo ""
	@echo "Development targets:"
	@echo "  dev-build        - Build in debug mode"
	@echo "  dev-test         - Run tests in debug mode"
	@echo "  format           - Format source code"
	@echo "  lint             - Run static analysis"
	@echo "  security-scan    - Run security scanning tools"
	@echo ""
	@echo "Static binary targets:"
	@echo "  static-build     - Build static binary (self-contained)"
	@echo "  static-test      - Run tests on static binary"
	@echo "  static-package   - Create platform-specific static binary package"
	@echo "  static-zip       - Create static binary ZIP package"
	@echo "  static-all       - Create all static binary formats"
	@echo ""
	@echo "Dependency management:"
	@echo "  deps             - Install dependencies"
	@echo "  dev-deps         - Install development tools"
	@echo ""
	@echo "Service management:"
	@echo "  service-install  - Install system service"
	@echo "  service-status   - Check service status"
	@echo "  service-start    - Start service"
	@echo "  service-stop     - Stop service"
	@echo ""
	@echo "Help categories:"
	@echo "  help-all         - Show all available targets"
	@echo "  help-build       - Build and development targets"
	@echo "  help-package     - Package creation targets"
	@echo "  help-deps        - Dependency management targets"
	@echo "  help-service     - Service management targets"
	@echo "  help-docker      - Docker targets"
	@echo "  help-config      - Configuration management targets"
	@echo "  help-platform    - Platform-specific targets"
	@echo ""
	@echo "Examples:"
	@echo "  make build       - Build the project"
	@echo "  make test        - Build and run tests"
	@echo "  make package     - Create platform-specific packages"
	@echo "  make dev-deps    - Install development tools"
	@echo "  make help-all    - Show all available targets"

# Help - All targets (comprehensive)
help-all:
	@echo "Simple NFS Daemon - A lightweight and secure NFS server - All Available Targets"
	@echo "=============================================="
	@echo ""
	@echo "Essential targets:"
	@echo "  all              - Build the project (default)"
	@echo "  build            - Build using CMake"
	@echo "  clean            - Clean build files"
	@echo "  install          - Install the project"
	@echo "  uninstall        - Uninstall the project"
	@echo "  test             - Run tests"
	@echo "  test-verbose     - Run tests with verbose output"
	@echo "  rebuild          - Clean and rebuild"
	@echo ""
	@echo "Package targets:"
	@echo "  package          - Build platform-specific packages"
	@echo "  package-source   - Create source code packages (TAR.GZ + ZIP)"
	@echo "  package-all      - Build all package formats"
	@echo "  package-info     - Show package information"
ifeq ($(PLATFORM),macos)
	@echo "  package-dmg      - Build DMG package (macOS only)"
	@echo "  package-pkg      - Build PKG package (macOS only)"
else ifeq ($(PLATFORM),linux)
	@echo "  package-rpm      - Build RPM package (Linux only)"
	@echo "  package-deb      - Build DEB package (Linux only)"
else ifeq ($(PLATFORM),windows)
	@echo "  package-msi      - Build MSI package (Windows only)"
	@echo "  package-zip      - Build ZIP package (Windows only)"
endif
	@echo ""
	@echo "Development targets:"
	@echo "  dev-build        - Build in debug mode"
	@echo "  dev-test         - Run tests in debug mode"
	@echo "  debug            - Build with debug information"
	@echo "  release          - Build with optimization"
	@echo "  sanitize         - Build with sanitizers"
	@echo "  docs             - Build documentation"
	@echo "  analyze          - Run static analysis"
	@echo ""
	@echo "Static binary targets:"
	@echo "  static-build     - Build static binary (self-contained)"
	@echo "  static-test      - Run tests on static binary"
	@echo "  static-package   - Create platform-specific static binary package"
	@echo "  static-zip       - Create static binary ZIP package"
	@echo "  static-all       - Create all static binary formats"
	@echo "  coverage         - Generate coverage report"
	@echo "  format           - Format source code"
	@echo "  check-style      - Check code style"
	@echo "  lint             - Run linting tools"
	@echo "  security-scan    - Run security scanning tools"
	@echo ""
	@echo "Dependency management:"
	@echo "  deps             - Install dependencies"
	@echo "  dev-deps         - Install development tools"
	@echo "  runtime-deps     - Runtime dependencies"
	@echo ""
	@echo "Service management:"
	@echo "  service-install  - Install system service"
	@echo "  service-uninstall- Uninstall system service"
	@echo "  service-status   - Check service status"
	@echo "  service-start    - Start service"
	@echo "  service-stop     - Stop service"
	@echo "  service-restart  - Restart service"
	@echo "  service-enable   - Enable service"
	@echo "  service-disable  - Disable service"
	@echo ""
	@echo "Docker targets:"
	@echo "  docker-build     - Build Docker image"
	@echo "  docker-run       - Run Docker container"
	@echo "  docker-stop      - Stop Docker container"
	@echo ""
	@echo "Configuration management:"
	@echo "  config-install   - Install configuration files"
	@echo "  config-backup    - Backup configuration"
	@echo "  log-rotate       - Install log rotation"
	@echo "  backup           - Create full backup"
	@echo "  restore          - Restore from backup"
	@echo ""
	@echo "Cleanup targets:"
	@echo "  distclean        - Clean all generated files"
	@echo ""
	@echo "Legacy targets:"
	@echo "  start            - Start service (legacy)"
	@echo "  stop             - Stop service (legacy)"
	@echo "  restart          - Restart service (legacy)"
	@echo "  status           - Check service status (legacy)"

# Package source code
package-source: build
	@echo "Creating source packages..."
	@mkdir -p $(DIST_DIR)
ifeq ($(PLATFORM),windows)
	@echo "Creating ZIP source package..."
	powershell -Command "Compress-Archive -Path '$(SRC_DIR)', '$(INCLUDE_DIR)', 'CMakeLists.txt', 'Makefile', 'README.md', 'LICENSE', 'deployment', 'config', 'scripts' -DestinationPath '$(DIST_DIR)\$(PROJECT_NAME)-$(VERSION)-src.zip' -Force"
	@echo "Creating TAR.GZ source package..."
	tar -czf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-src.tar.gz \
		$(SRC_DIR) \
		$(INCLUDE_DIR) \
		CMakeLists.txt \
		Makefile \
		README.md \
		LICENSE \
		deployment \
		config \
		scripts
else
	@echo "Creating TAR.GZ source package..."
	tar -czf $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-src.tar.gz \
		$(SRC_DIR) \
		$(INCLUDE_DIR) \
		CMakeLists.txt \
		Makefile \
		README.md \
		LICENSE \
		deployment \
		config \
		scripts
	@echo "Creating ZIP source package..."
	zip -r $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-src.zip \
		$(SRC_DIR) \
		$(INCLUDE_DIR) \
		CMakeLists.txt \
		Makefile \
		README.md \
		LICENSE \
		deployment \
		config \
		scripts
endif
	@echo "Source packages created: TAR.GZ and ZIP"

# Package all formats (binary + source)
package-all: package package-source
	@echo "All packages created successfully"
	@echo "Binary packages:"
	@ls -la $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.* 2>/dev/null || echo "No binary packages found"
	@echo "Source packages:"
	@ls -la $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-src.* 2>/dev/null || echo "No source packages found"

# Individual package targets for each format
package-deb: build
ifeq ($(PLATFORM),linux)
	@echo "Building DEB package..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G DEB
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.deb $(DIST_DIR)/
	@echo "DEB package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.deb"
else
	@echo "DEB packages are only supported on Linux"
endif

package-rpm: build
ifeq ($(PLATFORM),linux)
	@echo "Building RPM package..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G RPM
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.rpm $(DIST_DIR)/
	@echo "RPM package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.rpm"
else
	@echo "RPM packages are only supported on Linux"
endif

package-msi: build
ifeq ($(PLATFORM),windows)
	@echo "Building MSI package..."
	@$(MKDIR) $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G WIX
	$(CP) $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.msi $(DIST_DIR)/
	@echo "MSI package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.msi"
else
	@echo "MSI packages are only supported on Windows"
endif

package-exe: build
ifeq ($(PLATFORM),windows)
	@echo "Building EXE package..."
	@$(MKDIR) $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G ZIP
	$(CP) $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.zip $(DIST_DIR)/
	@echo "EXE package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.zip"
else
	@echo "EXE packages are only supported on Windows"
endif

package-dmg: build
ifeq ($(PLATFORM),macos)
	@echo "Building DMG package..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G DragNDrop
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.dmg $(DIST_DIR)/
	@echo "DMG package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.dmg"
else
	@echo "DMG packages are only supported on macOS"
endif

package-pkg: build
ifeq ($(PLATFORM),macos)
	@echo "Building PKG package..."
	@mkdir -p $(DIST_DIR)
	cd $(BUILD_DIR) && cpack -G productbuild
	mv $(BUILD_DIR)/$(PROJECT_NAME)-$(VERSION)-*.pkg $(DIST_DIR)/
	@echo "PKG package created: $(DIST_DIR)/$(PROJECT_NAME)-$(VERSION)-*.pkg"
else
	@echo "PKG packages are only supported on macOS"
endif

# Show package information
package-info:
	@echo "Package Information for $(PROJECT_NAME) $(VERSION)"
	@echo "=============================================="
	@echo "Platform: $(PLATFORM)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Distribution directory: $(DIST_DIR)"
	@echo ""
	@echo "Available package formats:"
ifeq ($(PLATFORM),linux)
	@echo "  - DEB (Debian/Ubuntu)"
	@echo "  - RPM (Red Hat/CentOS/Fedora)"
	@echo "  - TAR.GZ (Source)"
	@echo "  - ZIP (Source)"
else ifeq ($(PLATFORM),macos)
	@echo "  - DMG (macOS Disk Image)"
	@echo "  - PKG (macOS Installer)"
	@echo "  - TAR.GZ (Source)"
	@echo "  - ZIP (Source)"
else ifeq ($(PLATFORM),windows)
	@echo "  - MSI (Windows Installer)"
	@echo "  - ZIP (Windows Executable + Source)"
	@echo "  - TAR.GZ (Source)"
endif
	@echo ""
	@echo "To create packages:"
	@echo "  make package          - Create platform-specific packages"
	@echo "  make package-source   - Create source packages"
	@echo "  make package-all      - Create all packages"
	@echo "  make package-deb      - Create DEB package (Linux only)"
	@echo "  make package-rpm      - Create RPM package (Linux only)"
	@echo "  make package-msi      - Create MSI package (Windows only)"
	@echo "  make package-dmg      - Create DMG package (macOS only)"
	@echo "  make package-pkg      - Create PKG package (macOS only)"

# Legacy targets for backward compatibility
debug: dev-build
release: build
sanitize: dev-build
rebuild: clean build
test-verbose: test

# Service control targets (legacy)
start: service-start
stop: service-stop
restart: service-restart
status: service-status

# Additional service management targets
service-start:
ifeq ($(PLATFORM),macos)
	@echo "Starting service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl start com.$(PROJECT_NAME).$(PROJECT_NAME); \
		echo "Service started successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Starting service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl start $(PROJECT_NAME); \
		echo "Service started successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Starting service on Windows..."
	@sc start 
endif

service-stop:
ifeq ($(PLATFORM),macos)
	@echo "Stopping service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl stop com.$(PROJECT_NAME).$(PROJECT_NAME); \
		echo "Service stopped successfully"; \
	else \
		echo "Service not installed"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Stopping service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl stop $(PROJECT_NAME); \
		echo "Service stopped successfully"; \
	else \
		echo "Service not installed"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Stopping service on Windows..."
	@sc stop 
endif

service-restart:
ifeq ($(PLATFORM),macos)
	@echo "Restarting service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl stop com.$(PROJECT_NAME).$(PROJECT_NAME); \
		sleep 2; \
		sudo launchctl start com.$(PROJECT_NAME).$(PROJECT_NAME); \
		echo "Service restarted successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Restarting service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl restart $(PROJECT_NAME); \
		echo "Service restarted successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Restarting service on Windows..."
	@sc stop 
	@timeout /t 2 /nobreak >nul
	@sc start 
endif

service-enable:
ifeq ($(PLATFORM),macos)
	@echo "Enabling service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl load -w /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist; \
		echo "Service enabled successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Enabling service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl enable $(PROJECT_NAME); \
		echo "Service enabled successfully"; \
	else \
		echo "Service not installed. Run 'make service-install' first"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Enabling service on Windows..."
	@sc config  start= auto
endif

service-disable:
ifeq ($(PLATFORM),macos)
	@echo "Disabling service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl unload /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist; \
		echo "Service disabled successfully"; \
	else \
		echo "Service not installed"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Disabling service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl disable $(PROJECT_NAME); \
		echo "Service disabled successfully"; \
	else \
		echo "Service not installed"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Disabling service on Windows..."
	@sc config  start= disabled
endif

service-uninstall:
ifeq ($(PLATFORM),macos)
	@echo "Uninstalling service on macOS..."
	@if [ -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist ]; then \
		sudo launchctl unload /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist; \
		sudo rm -f /Library/LaunchDaemons/com.$(PROJECT_NAME).$(PROJECT_NAME).plist; \
		echo "Service uninstalled successfully"; \
	else \
		echo "Service not found"; \
	fi
else ifeq ($(PLATFORM),linux)
	@echo "Uninstalling service on Linux..."
	@if [ -f /etc/systemd/system/$(PROJECT_NAME).service ]; then \
		sudo systemctl stop $(PROJECT_NAME); \
		sudo systemctl disable $(PROJECT_NAME); \
		sudo rm -f /etc/systemd/system/$(PROJECT_NAME).service; \
		sudo systemctl daemon-reload; \
		echo "Service uninstalled successfully"; \
	else \
		echo "Service not found"; \
	fi
else ifeq ($(PLATFORM),windows)
	@echo "Uninstalling service on Windows..."
	@sc query  >nul 2>&1 && ( \
		sc stop ; \
		sc delete ; \
		echo "Service uninstalled successfully"; \
	) || echo "Service not found"
endif

# Configuration management
config-install: install
ifeq ($(PLATFORM),windows)
	$(MKDIR) "$(CONFIG_DIR)"
	$(CP) $(CONFIG_DIR_SRC)\* "$(CONFIG_DIR)\"
else
	sudo mkdir -p $(CONFIG_DIR)
	sudo cp -r $(CONFIG_DIR_SRC)/* $(CONFIG_DIR)/
	sudo chown -R root:root $(CONFIG_DIR)
	sudo chmod -R 644 $(CONFIG_DIR)
	sudo find $(CONFIG_DIR) -type d -exec chmod 755 {} \;
endif

config-backup:
	@$(MKDIR) $(DIST_DIR)/config-backup
	$(CP) $(CONFIG_DIR_SRC) $(DIST_DIR)/config-backup/
ifeq ($(PLATFORM),windows)
	powershell -Command "Compress-Archive -Path '$(DIST_DIR)\config-backup' -DestinationPath '$(DIST_DIR)\config-backup-$(VERSION).zip' -Force"
else
	tar -czf $(DIST_DIR)/config-backup-$(VERSION).tar.gz -C $(DIST_DIR) config-backup
endif

# Log management
log-rotate: install
ifeq ($(PLATFORM),linux)
	sudo cp $(DEPLOYMENT_DIR)/logrotate.d/$(PROJECT_NAME) /etc/logrotate.d/
	sudo chmod 644 /etc/logrotate.d/$(PROJECT_NAME)
else ifeq ($(PLATFORM),windows)
	@echo "Log rotation on Windows is handled by the Windows Event Log system"
	@echo "Configure in Event Viewer or use PowerShell commands"
else
	@echo "Log rotation not implemented for this platform"
endif

# Backup and restore
backup: config-backup
	@$(MKDIR) $(DIST_DIR)/backup
ifeq ($(PLATFORM),windows)
	powershell -Command "Compress-Archive -Path '$(CONFIG_DIR_SRC)', '$(DEPLOYMENT_DIR)', '$(SRC_DIR)', '$(INCLUDE_DIR)', 'CMakeLists.txt', 'Makefile', 'README.md', 'LICENSE' -DestinationPath '$(DIST_DIR)\$(PROJECT_NAME)-backup-$(VERSION).zip' -Force"
else
	tar -czf $(DIST_DIR)/$(PROJECT_NAME)-backup-$(VERSION).tar.gz \
		$(CONFIG_DIR_SRC) \
		$(DEPLOYMENT_DIR) \
		$(SRC_DIR) \
		$(INCLUDE_DIR) \
		CMakeLists.txt \
		Makefile \
		README.md \
		LICENSE
endif

restore: backup
	@echo "Restoring from backup..."
ifeq ($(PLATFORM),windows)
	@if exist $(DIST_DIR)\$(PROJECT_NAME)-backup-$(VERSION).zip ( \
		powershell -Command "Expand-Archive -Path '$(DIST_DIR)\$(PROJECT_NAME)-backup-$(VERSION).zip' -DestinationPath '.' -Force"; \
		echo Restore completed; \
	) else ( \
		echo No backup found at $(DIST_DIR)\$(PROJECT_NAME)-backup-$(VERSION).zip; \
	)
else
	@if [ -f $(DIST_DIR)/$(PROJECT_NAME)-backup-$(VERSION).tar.gz ]; then \
		tar -xzf $(DIST_DIR)/$(PROJECT_NAME)-backup-$(VERSION).tar.gz; \
		echo "Restore completed"; \
	else \
		echo "No backup found at $(DIST_DIR)/$(PROJECT_NAME)-backup-$(VERSION).tar.gz"; \
	fi
endif

# Cleanup
distclean: clean
	$(RM) $(DIST_DIR)
	$(RM) $(PACKAGE_DIR)
ifeq ($(PLATFORM),windows)
	for /r . %%i in (*.o *.a *.so *.dylib *.exe *.dll *.obj *.pdb *.ilk *.exp *.lib) do del "%%i" 2>nul
else
	find . -name "*.o" -delete
	find . -name "*.a" -delete
	find . -name "*.so" -delete
	find . -name "*.dylib" -delete
	find . -name "*.exe" -delete
	find . -name "*.dll" -delete
endif

# Phony targets
.PHONY: all build clean install uninstall test package package-source package-all \
        package-deb package-rpm package-msi package-exe package-dmg package-pkg package-info \
        static-build static-test static-package static-zip static-all \
        dev-build dev-test format check-style lint security-scan deps dev-deps \
        docker-build docker-run docker-stop service-install service-uninstall service-status \
        service-start service-stop service-restart service-enable service-disable \
        config-install config-backup log-rotate backup restore distclean \
        debug release sanitize rebuild test-verbose start stop restart status \
        version help help-all help-build help-package help-deps help-service help-docker help-config help-platform

# Default target
.DEFAULT_GOAL := all
