cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../manifest.json" SONY_XM5_MANIFEST)
    string(JSON SONY_XM5_VERSION GET "${SONY_XM5_MANIFEST}" version)
    add_compile_definitions(SONY_XM5_VERSION="${SONY_XM5_VERSION}")

    project(sony-ctl
        VERSION ${SONY_XM5_VERSION}
        DESCRIPTION "CLI for the Sony WH-1000XM5 and WF-1000XM5 daemon"
        LANGUAGES CXX
    )

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Default install prefix to ~/.local" FORCE)
    endif()

    add_compile_options(
        -Wall
        -Wextra
        -pedantic
        -Wno-unused-parameter
    )
endif()

add_executable(sony-ctl
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/src/Commands.cpp
)

target_include_directories(sony-ctl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/src
)

target_compile_features(sony-ctl PRIVATE cxx_std_20)

install(TARGETS sony-ctl RUNTIME DESTINATION bin)
