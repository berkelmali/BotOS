#!/usr/bin/env bash
# ============================================================
# BotOS Core — Clean Script
# ============================================================
# Removes all build artifacts, temporary files, and optionally
# Buildroot output.
#
# Usage:
#   ./scripts/clean.sh [OPTIONS]
#
# Options:
#   --all           Also clean Buildroot output & downloads
#   --help          Show this help message
# ============================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CLEAN_ALL=0

# ── Colors ─────────────────────────────────────────────────
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${CYAN}[Clean]${NC} $*"; }
log_ok()   { echo -e "${GREEN}[Clean]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[Clean]${NC} $*"; }

# ── Args ───────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)  CLEAN_ALL=1; shift ;;
        --help)
            head -n 14 "$0" | tail -n +3 | sed 's/^# //' | sed 's/^#//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║       BotOS Core — Clean                 ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""

# ── CMake Build ────────────────────────────────────────────
if [[ -d "${PROJECT_ROOT}/build" ]]; then
    log_info "Removing build/ directory..."
    rm -rf "${PROJECT_ROOT}/build"
    log_ok "build/ removed."
else
    log_info "build/ directory not found, skipping."
fi

# ── Stale object files ────────────────────────────────────
log_info "Removing stale .o / .d files..."
find "${PROJECT_ROOT}" -name '*.o' -o -name '*.d' | xargs rm -f 2>/dev/null || true
log_ok "Stale files removed."

# ── Buildroot (optional) ──────────────────────────────────
if [[ $CLEAN_ALL -eq 1 ]]; then
    log_warn "Full clean: removing Buildroot output..."
    rm -rf "${PROJECT_ROOT}/buildroot/output"
    rm -rf "${PROJECT_ROOT}/buildroot/dl"
    rm -f  "${PROJECT_ROOT}/buildroot/.config"
    log_ok "Buildroot artifacts removed."
fi

echo ""
log_ok "Clean complete."
echo ""
