cmake_minimum_required(VERSION 3.20)
project(DynamoBOT VERSION 1.0.0 LANGUAGES CXX)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)

# Options
option(DYNAMO_BUILD_TESTS "Build tests" OFF)
option(DYNAMO_BUILD_BACKEND_HOST "Build the standalone backend host" ON)

# Find required packages
find_package(Threads REQUIRED)
include(GNUInstallDirs)

# Include FetchContent for downloading dependencies
include(FetchContent)

# ASIO (standalone, header-only)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-29-0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

# nlohmann/json (header-only)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# cpp-httplib (header-only)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

# OpenSSL for HTTPS
find_package(OpenSSL REQUIRED)

set(DYNAMO_OPENSSL_RUNTIME_DLLS)
if(DEFINED OPENSSL_INCLUDE_DIR)
    cmake_path(GET OPENSSL_INCLUDE_DIR PARENT_PATH DYNAMO_OPENSSL_ROOT)
    if(DYNAMO_OPENSSL_ROOT)
        file(GLOB DYNAMO_OPENSSL_RUNTIME_DLLS
            "${DYNAMO_OPENSSL_ROOT}/bin/libssl-*.dll"
            "${DYNAMO_OPENSSL_ROOT}/bin/libcrypto-*.dll")
    endif()
endif()

# Shared backend sources
set(DYNAMO_BACKEND_SOURCES
    src/core/config.cpp
    src/config/config_service.cpp
    src/network/http_client.cpp
    src/network/kryo_serializer.cpp
    src/network/kryo_client.cpp
    src/network/packet_registry.cpp
    src/packets/auth_packets.cpp
    src/packets/api_packets.cpp
    src/game/game_engine.cpp
)

add_library(dynamo_backend STATIC ${DYNAMO_BACKEND_SOURCES})

target_include_directories(dynamo_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dynamo_backend PUBLIC
    asio
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ws2_32
    wsock32
)

target_compile_definitions(dynamo_backend PUBLIC
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(dynamo_backend PRIVATE /W4 /permissive-)
endif()

if(DYNAMO_BUILD_BACKEND_HOST)
    set(DYNAMO_HOST_SOURCES
        src/host/main.cpp
        src/host/backend_host.cpp
        src/host/ipc_pipe_server.cpp
    )

    add_executable(dynamo_backend_host ${DYNAMO_HOST_SOURCES})
    set_target_properties(dynamo_backend_host PROPERTIES OUTPUT_NAME "DynamoAPI")

    target_include_directories(dynamo_backend_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(dynamo_backend_host PRIVATE
        dynamo_backend
    )

    if(MSVC)
        target_compile_options(dynamo_backend_host PRIVATE /W4 /permissive-)
    endif()

    install(TARGETS dynamo_backend_host
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT backend_host_runtime)
    install(DIRECTORY config/profiles
        DESTINATION ${CMAKE_INSTALL_BINDIR}/config
        COMPONENT backend_host_runtime)
    if(DYNAMO_OPENSSL_RUNTIME_DLLS)
        install(FILES ${DYNAMO_OPENSSL_RUNTIME_DLLS}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT backend_host_runtime)
    endif()
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT backend_host_runtime)
    endif()
endif()
