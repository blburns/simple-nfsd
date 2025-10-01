# Tests CMakeLists.txt for simple-nfsd
# Copyright 2024 SimpleDaemons

# Find testing framework
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Try to find GTest using pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTEST REQUIRED gtest)
endif()

if(GTest_FOUND OR GTEST_FOUND)
    # Enable testing
    enable_testing()
    
    # Add test executable
    add_executable(simple-nfsd-tests
        test_main.cpp
        test_nfsd_app.cpp
    )
    
    # Link libraries
    target_link_libraries(simple-nfsd-tests
        GTest::GTest
        GTest::Main
    )
    
    # Add source files directly instead of linking to executable
target_sources(simple-nfsd-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/nfsd_app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/nfs_server_simple.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/rpc_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/auth_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_nfsv2_procedures.cpp
)

# Add test source files
target_sources(simple-nfsd-tests PRIVATE
    test_main.cpp
    test_nfsd_app.cpp
    test_minimal.cpp
)
    
    # Include directories
    target_include_directories(simple-nfsd-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    
    # Add test to CTest
    add_test(NAME simple-nfsd-tests COMMAND simple-nfsd-tests)
    
    # Set test properties
    set_tests_properties(simple-nfsd-tests PROPERTIES
        TIMEOUT 30
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    message(STATUS "Tests enabled with GTest")
else()
    message(STATUS "GTest not found, tests disabled")
endif()
