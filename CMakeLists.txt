cmake_minimum_required(VERSION 3.20)
project(VEIL
    VERSION 0.1.0
    DESCRIPTION "VPN/tunnel with encrypted transport"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(VEIL_BUILD_TESTS "Build tests" ON)
option(VEIL_ENABLE_SANITIZERS "Enable sanitizers in debug builds" ON)
option(VEIL_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)

# Compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-unused-parameter
    $<$<CXX_COMPILER_ID:Clang>:-Wno-gnu-zero-variadic-macro-arguments>
)

# Sanitizers for debug builds
if(VEIL_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# clang-tidy
if(VEIL_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
    endif()
endif()

# Dependencies via FetchContent
include(FetchContent)

# fmt library
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
    GIT_SHALLOW TRUE
)

# spdlog library
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    GIT_SHALLOW TRUE
)

# CLI11 library
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
    GIT_SHALLOW TRUE
)

# Make fmt and spdlog available
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt spdlog cli11)

# Find libsodium (system or provide fallback)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SODIUM IMPORTED_TARGET libsodium)
endif()

if(NOT SODIUM_FOUND)
    find_library(SODIUM_LIBRARY NAMES sodium libsodium)
    find_path(SODIUM_INCLUDE_DIR sodium.h)
    if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
        set(SODIUM_FOUND TRUE)
        add_library(PkgConfig::SODIUM INTERFACE IMPORTED)
        set_target_properties(PkgConfig::SODIUM PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${SODIUM_LIBRARY}"
        )
    endif()
endif()

if(NOT SODIUM_FOUND)
    message(STATUS "libsodium not found on system, fetching from source...")
    FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG 1.0.19
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(libsodium)
    if(NOT libsodium_POPULATED)
        FetchContent_Populate(libsodium)
        # Build libsodium using its configure/make
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E make_directory ${libsodium_BINARY_DIR}
            WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
        )
        execute_process(
            COMMAND ./autogen.sh
            WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
            OUTPUT_QUIET ERROR_QUIET
        )
        execute_process(
            COMMAND ./configure --prefix=${libsodium_BINARY_DIR}/install --disable-shared --enable-static
            WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
            OUTPUT_QUIET ERROR_QUIET
        )
        execute_process(
            COMMAND make -j install
            WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
            OUTPUT_QUIET ERROR_QUIET
        )
        add_library(PkgConfig::SODIUM INTERFACE IMPORTED)
        set_target_properties(PkgConfig::SODIUM PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${libsodium_BINARY_DIR}/install/include"
            INTERFACE_LINK_LIBRARIES "${libsodium_BINARY_DIR}/install/lib/libsodium.a"
        )
        set(SODIUM_FOUND TRUE)
    endif()
endif()

if(NOT SODIUM_FOUND)
    message(FATAL_ERROR "libsodium is required but could not be found or built")
endif()

# VEIL library
add_library(veil STATIC
    src/crypto/crypto.cpp
    src/crypto/x25519.cpp
    src/crypto/hkdf.cpp
    src/crypto/chacha20poly1305.cpp
    src/packet/packet_builder.cpp
    src/packet/packet_parser.cpp
    src/packet/frame.cpp
    src/mux/replay_window.cpp
    src/mux/session_rotator.cpp
    src/mux/rate_limiter.cpp
    src/mux/ack_bitmap.cpp
    src/mux/reorder_buffer.cpp
    src/mux/fragment_assembler.cpp
    src/mux/retransmission.cpp
    src/handshake/handshake.cpp
    src/transport/udp_socket.cpp
    src/transport/transport_session.cpp
    src/config/config.cpp
    src/utils/time.cpp
    src/utils/logging.cpp
)

target_include_directories(veil PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(veil PUBLIC
    fmt::fmt
    spdlog::spdlog
    CLI11::CLI11
    PkgConfig::SODIUM
)

# Tests
if(VEIL_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)

    add_executable(veil_tests
        tests/crypto_test.cpp
        tests/packet_test.cpp
        tests/mux_test.cpp
        tests/handshake_test.cpp
        tests/transport_test.cpp
        tests/integration_test.cpp
    )

    target_link_libraries(veil_tests PRIVATE
        veil
        GTest::gtest_main
        GTest::gmock
    )

    gtest_discover_tests(veil_tests)
endif()

# Example/demo executable
add_executable(veil_demo examples/demo.cpp)
target_link_libraries(veil_demo PRIVATE veil)
