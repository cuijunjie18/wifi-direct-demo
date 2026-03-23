cmake_minimum_required(VERSION 3.20)
project(WiFiDirectCli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Windows SDK cppwinrt.exe and generate projection headers
find_program(CPPWINRT_EXE cppwinrt
    HINTS "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22000.0/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
)

if(NOT CPPWINRT_EXE)
    message(FATAL_ERROR "cppwinrt.exe not found. Install Windows SDK 10.0.19041.0 or later.")
endif()

# Generate C++/WinRT projection headers into build directory
set(CPPWINRT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cppwinrt")
file(MAKE_DIRECTORY "${CPPWINRT_OUTPUT_DIR}")

add_custom_command(
    OUTPUT "${CPPWINRT_OUTPUT_DIR}/winrt/base.h"
    COMMAND "${CPPWINRT_EXE}" -input sdk -output "${CPPWINRT_OUTPUT_DIR}"
    COMMENT "Generating C++/WinRT projection headers..."
)
add_custom_target(cppwinrt_headers DEPENDS "${CPPWINRT_OUTPUT_DIR}/winrt/base.h")

add_executable(wifi_direct_cli
    main.cpp
    wifi_direct_manager.h
    wifi_direct_manager.cpp
)

add_dependencies(wifi_direct_cli cppwinrt_headers)

target_include_directories(wifi_direct_cli PRIVATE "${CPPWINRT_OUTPUT_DIR}")

# Link against WindowsApp.lib for WinRT support
target_link_libraries(wifi_direct_cli PRIVATE windowsapp)

# MSVC specific flags
if(MSVC)
    target_compile_options(wifi_direct_cli PRIVATE /await:strict /EHsc /W4)
endif()
