cmake_minimum_required(VERSION 3.20)

# manifest.json holds the one version of the project: the widget reads it,
# and the daemon and the CLI report it, so the panel can tell when the
# installed daemon is older than the widget.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" SONY_XM5_MANIFEST)
string(JSON SONY_XM5_VERSION GET "${SONY_XM5_MANIFEST}" version)

project(omarchy-sony-xm5
    VERSION ${SONY_XM5_VERSION}
    DESCRIPTION "Sony WH-1000XM5 and WF-1000XM5 daemon, CLI and Omarchy widget"
    LANGUAGES CXX C
)

# Standard C++20 enforcement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd and language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default installation directory to ~/.local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Default install prefix to ~/.local" FORCE)
endif()

# Compiler options: Strict warnings as required
add_compile_options(
    -Wall
    -Wextra
    -pedantic
    -Wno-unused-parameter
)

add_compile_definitions(SONY_XM5_VERSION="${SONY_XM5_VERSION}")

# Position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Testing configuration
include(CTest)
enable_testing()

# Daemon & Protocol Library Subdirectory
add_subdirectory(daemon)

# Optional CLI Subdirectory (Milestone 3)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli/CMakeLists.txt")
    add_subdirectory(cli)
endif()
