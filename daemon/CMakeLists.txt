cmake_minimum_required(VERSION 3.20)

# Package Resolution via PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ REQUIRED IMPORTED_TARGET bluez)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Target 1: sony_protocol_lib (Static Protocol, State & Bluetooth Transport Library)
# ---------------------------------------------------------------------------
set(PROTOCOL_LIB_SOURCES
    src/MDRProtocol.cpp
    src/HeadphoneState.cpp
    src/MdrLink.cpp
    src/HeadsetSession.cpp
    src/SessionManager.cpp
    src/Commands.cpp
    src/FakeHeadset.cpp
    src/BluetoothManager.cpp
    src/StateEngine.cpp
    src/IpcServer.cpp
)

add_library(sony_protocol_lib STATIC
    ${PROTOCOL_LIB_SOURCES}
)

target_include_directories(sony_protocol_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(sony_protocol_lib PUBLIC
    PkgConfig::BLUEZ
    PkgConfig::DBUS
    Threads::Threads
)

target_compile_features(sony_protocol_lib PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Target 2: test_protocol (Protocol, Framing, StateEngine & IpcServer Tests)
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    foreach(test_name test_protocol test_session test_ipc)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE sony_protocol_lib)
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_compile_definitions(${test_name} PRIVATE
            SONY_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
        )
    endforeach()

    add_test(NAME ProtocolTest COMMAND test_protocol)
    add_test(NAME SessionTest COMMAND test_session)
    add_test(NAME IpcTest COMMAND test_ipc)
endif()

# ---------------------------------------------------------------------------
# Target 3: sony-headphones-daemon (Headless Daemon Executable)
# ---------------------------------------------------------------------------
add_executable(sony-headphones-daemon
    src/main.cpp
)

target_link_libraries(sony-headphones-daemon PRIVATE
    sony_protocol_lib
)

target_include_directories(sony-headphones-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

install(TARGETS sony-headphones-daemon RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Target 4: systemd User Service Unit
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sony-headphones.service")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/sony-headphones.service"
        DESTINATION "lib/systemd/user"
    )
endif()
