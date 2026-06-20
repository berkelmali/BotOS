#!/usr/bin/env bash
# ============================================================
# BotOS Core — Build Script
# ============================================================
# Orchestrates the CMake build pipeline for all BotOS
# components. Supports both native and cross-compilation.
#
# Usage:
#   ./scripts/build.sh [OPTIONS]
#
# Options:
#   --release       Build in Release mode (default: Debug)
#   --cross         Use cross-compilation toolchain
#   --tests         Build with tests enabled
#   --no-apps       Skip L6 application builds
#   --clean-first   Clean build directory before building
#   --jobs N        Number of parallel jobs (default: auto)
#   --help          Show this help message
# ============================================================

set -euo pipefail

# ── Defaults ───────────────────────────────────────────────
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="Debug"
CROSS_COMPILE=0
BUILD_TESTS="ON"
BUILD_APPS="ON"
CLEAN_FIRST=0
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ── Colors ─────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info()  { echo -e "${CYAN}[BotOS]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[  OK ]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[ WARN]${NC} $*"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $*"; }

# ── Argument Parsing ───────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)      BUILD_TYPE="Release"; shift ;;
        --cross)        CROSS_COMPILE=1; shift ;;
        --tests)        BUILD_TESTS="ON"; shift ;;
        --no-apps)      BUILD_APPS="OFF"; shift ;;
        --clean-first)  CLEAN_FIRST=1; shift ;;
        --jobs)         JOBS="$2"; shift 2 ;;
        --help)
            head -n 18 "$0" | tail -n +3 | sed 's/^# //' | sed 's/^#//'
            exit 0
            ;;
        *)
            log_err "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ── Banner ─────────────────────────────────────────────────
echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║       BotOS Core — Build Pipeline        ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""

# ── Clean (optional) ──────────────────────────────────────
if [[ $CLEAN_FIRST -eq 1 ]]; then
    log_warn "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

# ── Configure ──────────────────────────────────────────────
log_info "Configuring (${BUILD_TYPE})..."

CMAKE_ARGS=(
    -S "${PROJECT_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DBOTOS_BUILD_TESTS="${BUILD_TESTS}"
    -DBOTOS_BUILD_APPS="${BUILD_APPS}"
)

if [[ $CROSS_COMPILE -eq 1 ]]; then
    log_info "Cross-compilation enabled (x86_64 toolchain)"
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/toolchain-x86_64.cmake")
fi

cmake "${CMAKE_ARGS[@]}"
log_ok "Configuration complete."

# ── Build ──────────────────────────────────────────────────
log_info "Building with ${JOBS} parallel jobs..."
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
log_ok "Build complete."

# ── Summary ────────────────────────────────────────────────
echo ""
log_ok "All targets built successfully!"
log_info "Binaries : ${BUILD_DIR}/bin/"
log_info "Libraries: ${BUILD_DIR}/lib/"

if [[ "${BUILD_TESTS}" == "ON" ]]; then
    echo ""
    log_info "Run tests with: cd ${BUILD_DIR} && ctest --output-on-failure"
fi

echo ""
