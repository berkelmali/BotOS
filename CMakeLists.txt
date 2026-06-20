# ============================================================
# BotOS Core — Top-Level CMake Configuration
# Version: 0.3.0
# ============================================================
cmake_minimum_required(VERSION 3.20)

project(BotOS
    VERSION     0.3.0
    DESCRIPTION "BotOS Core — A Developer-First Software Development Platform"
    HOMEPAGE_URL "https://github.com/berkelma/botos"
    LANGUAGES   C
)

# ── Build Options ──────────────────────────────────────────
option(BOTOS_BUILD_TESTS       "Build the BotOS test suite"           ON)
option(BOTOS_BUILD_APPS        "Build L6 user applications"           ON)
option(BOTOS_ENABLE_PYBRIDGE   "Enable Python/PyBridge C-to-Python bridge" OFF)
option(BOTOS_BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)

# ── Global Compiler Settings ──────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Position-independent code for shared library compatibility
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Global warning flags
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -Wshadow
    -Wdouble-promotion
    -Wformat=2
    -Wno-unused-parameter       # Allow unused params in stubs during dev
    -fstack-protector-strong
)

# POSIX compliance
add_compile_definitions(
    _POSIX_C_SOURCE=200809L
    _GNU_SOURCE
    BOTOS_VERSION="${PROJECT_VERSION}"
)

# ── Build Type Defaults ───────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

message(STATUS "")
message(STATUS "  ╔══════════════════════════════════════════╗")
message(STATUS "  ║         BotOS Core v${PROJECT_VERSION}              ║")
message(STATUS "  ║   A Developer-First Platform             ║")
message(STATUS "  ╚══════════════════════════════════════════╝")
message(STATUS "")
message(STATUS "  Build Type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler       : ${CMAKE_C_COMPILER}")
message(STATUS "  Build Tests      : ${BOTOS_BUILD_TESTS}")
message(STATUS "  Build Apps       : ${BOTOS_BUILD_APPS}")
message(STATUS "  PyBridge         : ${BOTOS_ENABLE_PYBRIDGE}")
message(STATUS "")

# ── Output Directories ────────────────────────────────────
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Subsystems ─────────────────────────────────────────────
# L3: Abstraction Layer
add_subdirectory(core)

# L4: Development Layer
add_subdirectory(shell)
add_subdirectory(sdk)

# L5: Platform Services
add_subdirectory(services)

# L6: User Applications
if(BOTOS_BUILD_APPS)
    add_subdirectory(apps)
endif()

# Tests
if(BOTOS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install Rules ──────────────────────────────────────────
include(GNUInstallDirs)

install(
    EXPORT BotOSTargets
    FILE BotOSTargets.cmake
    NAMESPACE BotOS::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BotOS
)
