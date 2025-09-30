@echo off
REM Build script for Windows systems
REM simple-nfsd - Build Script

setlocal enabledelayedexpansion

REM Set error handling
set "EXIT_CODE=0"

REM Colors for output (Windows 10+ supports ANSI colors)
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

REM Script configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "DIST_DIR=%PROJECT_ROOT%\dist"

REM Build options
set "BUILD_TYPE=Release"
set "BUILD_SHARED_LIBS=OFF"
set "BUILD_TESTS=ON"
set "ENABLE_SSL=ON"
set "ENABLE_JSON=ON"
set "ENABLE_STATIC_LINKING=OFF"
set "PACKAGE=false"
set "INSTALL=false"
set "CLEAN=false"

REM Function to print colored output
:print_status
echo %BLUE%[INFO]%NC% %~1
goto :eof

:print_success
echo %GREEN%[SUCCESS]%NC% %~1
goto :eof

:print_warning
echo %YELLOW%[WARNING]%NC% %~1
goto :eof

:print_error
echo %RED%[ERROR]%NC% %~1
goto :eof

REM Function to check if command exists
:command_exists
where "%~1" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "COMMAND_EXISTS=true"
) else (
    set "COMMAND_EXISTS=false"
)
goto :eof

REM Function to detect Windows version
:detect_windows
call :print_status "Detecting Windows version..."
for /f "tokens=4-5 delims=. " %%i in ('ver') do set VERSION=%%i.%%j
call :print_status "Windows version: %VERSION%"

REM Check if running on Windows 10 or later
for /f "tokens=2 delims=." %%a in ('ver') do set MINOR=%%a
if %MINOR% LSS 10 (
    call :print_warning "Windows 10 or later recommended for best compatibility"
)
goto :eof

REM Function to check Visual Studio installation
:check_visual_studio
call :print_status "Checking Visual Studio installation..."

REM Check for Visual Studio 2017 or later
set "VS_FOUND=false"
set "VS_VERSION="

REM Check common Visual Studio installation paths
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\*\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_FOUND=true"
    set "VS_VERSION=2017"
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2017"
) else if exist "C:\Program Files\Microsoft Visual Studio\2019\*\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_FOUND=true"
    set "VS_VERSION=2019"
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_FOUND=true"
    set "VS_VERSION=2022"
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022"
)

if "%VS_FOUND%"=="true" (
    call :print_success "Found Visual Studio %VS_VERSION%"
    set "VS_DEVENV=%VS_PATH%\*\Common7\IDE\devenv.exe"
    for %%i in ("%VS_DEVENV%") do set "VS_DEVENV=%%~fi"
) else (
    call :print_error "Visual Studio not found. Please install Visual Studio 2017 or later with C++ support."
    set "EXIT_CODE=1"
    goto :end
)
goto :eof

REM Function to check CMake installation
:check_cmake
call :print_status "Checking CMake installation..."
call :command_exists cmake
if "%COMMAND_EXISTS%"=="false" (
    call :print_error "CMake not found. Please install CMake from https://cmake.org/download/"
    set "EXIT_CODE=1"
    goto :end
) else (
    call :print_success "CMake found"
    cmake --version | findstr "cmake version"
)
goto :eof

REM Function to check Git installation
:check_git
call :print_status "Checking Git installation..."
call :command_exists git
if "%COMMAND_EXISTS%"=="false" (
    call :print_error "Git not found. Please install Git from https://git-scm.com/download/win"
    set "EXIT_CODE=1"
    goto :end
) else (
    call :print_success "Git found"
    git --version
)
goto :eof

REM Function to install dependencies using vcpkg
:install_dependencies
call :print_status "Installing build dependencies using vcpkg..."

REM Check if vcpkg exists
if not exist "%PROJECT_ROOT%\vcpkg" (
    call :print_status "Installing vcpkg..."
    cd /d "%PROJECT_ROOT%"
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    call bootstrap-vcpkg.bat
) else (
    call :print_status "vcpkg found, updating..."
    cd /d "%PROJECT_ROOT%\vcpkg"
    git pull
    call bootstrap-vcpkg.bat
)

REM Install required packages
call :print_status "Installing required packages..."
cd /d "%PROJECT_ROOT%\vcpkg"
vcpkg install openssl:x64-windows
vcpkg install jsoncpp:x64-windows
vcpkg install zlib:x64-windows

REM Install optional development tools
vcpkg install cppcheck:x64-windows
vcpkg install gtest:x64-windows

call :print_success "Dependencies installed successfully"
goto :eof

REM Function to clean build directory
:clean_build
if "%CLEAN%"=="true" (
    call :print_status "Cleaning build directory..."
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
    call :print_success "Build directory cleaned"
)
goto :eof

REM Function to build project
:build_project
call :print_status "Building simple-nfsd..."

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Configure CMake
cmake .. ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DBUILD_SHARED_LIBS=%BUILD_SHARED_LIBS% ^
    -DENABLE_TESTS=%BUILD_TESTS% ^
    -DENABLE_SSL=%ENABLE_SSL% ^
    -DENABLE_JSON=%ENABLE_JSON% ^
    -DENABLE_STATIC_LINKING=%ENABLE_STATIC_LINKING% ^
    -DENABLE_PACKAGING=ON ^
    -DCMAKE_TOOLCHAIN_FILE="%PROJECT_ROOT%\vcpkg\scripts\buildsystems\vcpkg.cmake"

if %ERRORLEVEL% NEQ 0 (
    call :print_error "CMake configuration failed"
    set "EXIT_CODE=1"
    goto :end
)

REM Build project
cmake --build . --config %BUILD_TYPE% --parallel

if %ERRORLEVEL% NEQ 0 (
    call :print_error "Build failed"
    set "EXIT_CODE=1"
    goto :end
)

call :print_success "Build completed successfully"
goto :eof

REM Function to run tests
:run_tests
if "%BUILD_TESTS%"=="ON" (
    call :print_status "Running tests..."
    cd /d "%BUILD_DIR%"
    
    ctest --output-on-failure --config %BUILD_TYPE%
    
    if %ERRORLEVEL% NEQ 0 (
        call :print_warning "Some tests failed, but continuing..."
    ) else (
        call :print_success "All tests passed"
    )
)
goto :eof

REM Function to install project
:install_project
if "%INSTALL%"=="true" (
    call :print_status "Installing simple-nfsd..."
    cd /d "%BUILD_DIR%"
    
    cmake --install . --config %BUILD_TYPE%
    
    if %ERRORLEVEL% NEQ 0 (
        call :print_error "Installation failed"
        set "EXIT_CODE=1"
        goto :end
    )
    
    call :print_success "Installation successful"
)
goto :eof

REM Function to create packages
:create_packages
if "%PACKAGE%"=="true" (
    call :print_status "Creating Windows packages..."
    cd /d "%BUILD_DIR%"
    
    REM Create distribution directory
    if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
    
    REM Create packages using CPack
    cpack
    
    REM Move packages to dist directory
    move *.msi "%DIST_DIR%\" 2>nul
    move *.exe "%DIST_DIR%\" 2>nul
    
    call :print_success "Packages created in %DIST_DIR%"
    dir "%DIST_DIR%"
)
goto :eof

REM Function to create static binary package
:create_static_package
if "%ENABLE_STATIC_LINKING%"=="ON" (
    call :print_status "Creating static binary package..."
    
    REM Create distribution directory
    if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
    
    REM Create static binary directory
    set "STATIC_DIR=%DIST_DIR%\simple-nfsd--static-windows"
    if not exist "%STATIC_DIR%" mkdir "%STATIC_DIR%"
    
    REM Copy binary and files
    copy "%BUILD_DIR%\simple-nfsd.exe" "%STATIC_DIR%\"
    copy "%PROJECT_ROOT%\README.md" "%STATIC_DIR%\"
    copy "%PROJECT_ROOT%\LICENSE" "%STATIC_DIR%\"
    if exist "%PROJECT_ROOT%\config" xcopy "%PROJECT_ROOT%\config" "%STATIC_DIR%\config\" /E /I
    
    REM Create ZIP package
    cd /d "%DIST_DIR%"
    powershell -Command "Compress-Archive -Path 'simple-nfsd--static-windows' -DestinationPath 'simple-nfsd--static-windows.zip' -Force"
    rmdir /s /q "simple-nfsd--static-windows"
    
    call :print_success "Static binary package created: simple-nfsd--static-windows.zip"
)
goto :eof

REM Function to show usage
:show_usage
echo simple-nfsd - Windows Build Script
echo.
echo Usage: %0 [OPTIONS]
echo.
echo Options:
echo     -h, --help              Show this help message
echo     -d, --deps              Install dependencies only
echo     -b, --build             Build project only
echo     -t, --test              Run tests only
echo     -i, --install           Install project only
echo     -p, --package           Create packages only
echo     -a, --all               Full build and install (default)
echo     --static                Build static binary
echo     --debug                 Build in debug mode
echo     --clean                 Clean build directory before building
echo     --no-tests              Disable tests
echo     --no-ssl                Disable SSL support
echo     --no-json               Disable JSON support
echo.
echo Examples:
echo     %0                      # Full build and install
echo     %0 --deps               # Install dependencies only
echo     %0 --build --test       # Build and test
echo     %0 --static --package   # Build static binary and create packages
echo     %0 --clean --all        # Clean build and full install
echo.
goto :eof

REM Function to parse command line arguments
:parse_arguments
:parse_loop
if "%~1"=="" goto :parse_done
if "%~1"=="-h" goto :parse_help
if "%~1"=="--help" goto :parse_help
if "%~1"=="-d" goto :parse_deps
if "%~1"=="--deps" goto :parse_deps
if "%~1"=="-b" goto :parse_build
if "%~1"=="--build" goto :parse_build
if "%~1"=="-t" goto :parse_test
if "%~1"=="--test" goto :parse_test
if "%~1"=="-i" goto :parse_install
if "%~1"=="--install" goto :parse_install
if "%~1"=="-p" goto :parse_package
if "%~1"=="--package" goto :parse_package
if "%~1"=="-a" goto :parse_all
if "%~1"=="--all" goto :parse_all
if "%~1"=="--static" goto :parse_static
if "%~1"=="--debug" goto :parse_debug
if "%~1"=="--clean" goto :parse_clean
if "%~1"=="--no-tests" goto :parse_no_tests
if "%~1"=="--no-ssl" goto :parse_no_ssl
if "%~1"=="--no-json" goto :parse_no_json
call :print_error "Unknown option: %~1"
call :show_usage
set "EXIT_CODE=1"
goto :end

:parse_help
call :show_usage
exit /b 0

:parse_deps
set "DEPENDENCIES_ONLY=true"
shift
goto :parse_loop

:parse_build
set "BUILD_ONLY=true"
shift
goto :parse_loop

:parse_test
set "TEST_ONLY=true"
shift
goto :parse_loop

:parse_install
set "INSTALL_ONLY=true"
shift
goto :parse_loop

:parse_package
set "PACKAGE_ONLY=true"
shift
goto :parse_loop

:parse_all
set "ALL=true"
shift
goto :parse_loop

:parse_static
set "ENABLE_STATIC_LINKING=ON"
shift
goto :parse_loop

:parse_debug
set "BUILD_TYPE=Debug"
shift
goto :parse_loop

:parse_clean
set "CLEAN=true"
shift
goto :parse_loop

:parse_no_tests
set "BUILD_TESTS=OFF"
shift
goto :parse_loop

:parse_no_ssl
set "ENABLE_SSL=OFF"
shift
goto :parse_loop

:parse_no_json
set "ENABLE_JSON=OFF"
shift
goto :parse_loop

:parse_done
REM Set default action if none specified
if not defined DEPENDENCIES_ONLY if not defined BUILD_ONLY if not defined TEST_ONLY if not defined INSTALL_ONLY if not defined PACKAGE_ONLY if not defined ALL (
    set "ALL=true"
)
goto :eof

REM Main execution
:main
call :print_status "Starting simple-nfsd build process..."

REM Parse command line arguments
call :parse_arguments %*

REM Check system requirements
call :detect_windows
call :check_visual_studio
call :check_cmake
call :check_git

REM Install dependencies
if defined DEPENDENCIES_ONLY (
    call :install_dependencies
    call :print_success "Dependencies installed successfully"
    goto :end
) else if defined ALL (
    call :install_dependencies
)

REM Clean build directory
call :clean_build

REM Build project
if defined BUILD_ONLY (
    call :build_project
    call :print_success "Build completed successfully"
    goto :end
) else if defined ALL (
    call :build_project
)

REM Run tests
if defined TEST_ONLY (
    call :run_tests
    call :print_success "Tests completed"
    goto :end
) else if defined ALL (
    call :run_tests
)

REM Install project
if defined INSTALL_ONLY (
    set "INSTALL=true"
    call :install_project
    call :print_success "Installation completed successfully"
    goto :end
) else if defined ALL (
    set "INSTALL=true"
    call :install_project
)

REM Create packages
if defined PACKAGE_ONLY (
    set "PACKAGE=true"
    call :create_packages
    call :create_static_package
    call :print_success "Packages created successfully"
    goto :end
) else if defined ALL (
    set "PACKAGE=true"
    call :create_packages
    call :create_static_package
)

call :print_success "simple-nfsd build process completed successfully!"
goto :end

:end
exit /b %EXIT_CODE%
