# ============================================================
# BotOS Core — Cross-Compilation Toolchain (x86_64)
# ============================================================
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64.cmake \
#         -DBOTOS_TOOLCHAIN_DIR=/path/to/buildroot/output/host/bin ..
#
# This file configures CMake for cross-compiling BotOS
# components using a Buildroot-generated GCC toolchain.
# ============================================================

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ── Toolchain Prefix ──────────────────────────────────────
# BOTOS_TOOLCHAIN_DIR should point to the Buildroot host bin/
# e.g. /opt/buildroot/output/host/bin
if(NOT DEFINED BOTOS_TOOLCHAIN_DIR)
    set(BOTOS_TOOLCHAIN_DIR "/opt/botos-toolchain/bin"
        CACHE PATH "Path to the cross-compilation toolchain bin directory")
endif()

set(CROSS_COMPILE_PREFIX "x86_64-buildroot-linux-gnu-")

# ── Compilers ──────────────────────────────────────────────
set(CMAKE_C_COMPILER   "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}g++")
set(CMAKE_AR           "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}ar")
set(CMAKE_RANLIB       "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}ranlib")
set(CMAKE_STRIP        "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}strip")
set(CMAKE_LINKER       "${BOTOS_TOOLCHAIN_DIR}/${CROSS_COMPILE_PREFIX}ld")

# ── Sysroot ────────────────────────────────────────────────
if(DEFINED BOTOS_SYSROOT)
    set(CMAKE_SYSROOT "${BOTOS_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${BOTOS_SYSROOT}")
endif()

# ── Search Path Configuration ─────────────────────────────
# Never search the host for programs (use target only)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Only search the target sysroot for libraries and headers
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── Target Flags ───────────────────────────────────────────
set(CMAKE_C_FLAGS_INIT   "-march=x86-64 -mtune=generic")
# set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

