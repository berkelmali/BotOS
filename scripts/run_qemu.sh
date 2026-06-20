#!/usr/bin/env bash
# ============================================================
# BotOS Core — QEMU Launch Script
# ============================================================
# Launches BotOS in QEMU for rapid development testing.
#
# Usage:
#   ./scripts/run_qemu.sh [OPTIONS]
#
# Options:
#   --graphic       Launch with graphical display (default: nographic)
#   --memory N      RAM in MB (default: 512)
#   --smp N         Number of CPU cores (default: 2)
#   --debug         Enable GDB server on port 1234
#   --kernel PATH   Path to kernel bzImage
#   --rootfs PATH   Path to rootfs image
#   --help          Show this help message
# ============================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Defaults ───────────────────────────────────────────────
MEMORY="512"
SMP="2"
GRAPHIC=0
DEBUG=0
KERNEL="${PROJECT_ROOT}/buildroot/output/images/bzImage"
ROOTFS="${PROJECT_ROOT}/buildroot/output/images/rootfs.ext4"

# ── Colors ─────────────────────────────────────────────────
CYAN='\033[0;36m'
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

log_info() { echo -e "${CYAN}[QEMU]${NC} $*"; }
log_err()  { echo -e "${RED}[QEMU]${NC} $*"; }
log_ok()   { echo -e "${GREEN}[QEMU]${NC} $*"; }

# ── Argument Parsing ───────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --graphic)   GRAPHIC=1; shift ;;
        --memory)    MEMORY="$2"; shift 2 ;;
        --smp)       SMP="$2"; shift 2 ;;
        --debug)     DEBUG=1; shift ;;
        --kernel)    KERNEL="$2"; shift 2 ;;
        --rootfs)    ROOTFS="$2"; shift 2 ;;
        --help)
            head -n 17 "$0" | tail -n +3 | sed 's/^# //' | sed 's/^#//'
            exit 0
            ;;
        *)
            log_err "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ── Validate ───────────────────────────────────────────────
if [[ ! -f "${KERNEL}" ]]; then
    log_err "Kernel image not found: ${KERNEL}"
    log_err "Run a full Buildroot build first, or specify --kernel PATH"
    exit 1
fi

if [[ ! -f "${ROOTFS}" ]]; then
    log_err "Root filesystem not found: ${ROOTFS}"
    log_err "Run a full Buildroot build first, or specify --rootfs PATH"
    exit 1
fi

# ── Build QEMU Command ────────────────────────────────────
QEMU_ARGS=(
    qemu-system-x86_64
    -cpu max
    -m "${MEMORY}"
    -smp "${SMP}"
    -kernel "${KERNEL}"
    -drive "file=${ROOTFS},format=raw,if=virtio"
    -append "root=/dev/vda rw console=ttyS0 loglevel=7"
    -netdev "user,id=net0,hostfwd=tcp::2222-:22"
    -device "virtio-net-pci,netdev=net0"
    -virtfs "local,path=${PROJECT_ROOT},mount_tag=hostshare,security_model=mapped-xattr,id=host0"
)

if [[ $GRAPHIC -eq 0 ]]; then
    QEMU_ARGS+=(-nographic)
else
    QEMU_ARGS+=(-vga virtio -display sdl)
fi

if [[ $DEBUG -eq 1 ]]; then
    QEMU_ARGS+=(-s -S)
    log_info "GDB server enabled on tcp::1234"
    log_info "Attach with: gdb -ex 'target remote :1234'"
fi

# ── Launch ─────────────────────────────────────────────────
echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║       BotOS Core — QEMU Launcher         ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""
log_info "Kernel : ${KERNEL}"
log_info "RootFS : ${ROOTFS}"
log_info "Memory : ${MEMORY} MB"
log_info "CPUs   : ${SMP}"
log_info "Mode   : $([ $GRAPHIC -eq 1 ] && echo 'Graphic' || echo 'Serial Console')"
echo ""
log_ok "Starting QEMU..."
echo ""

exec "${QEMU_ARGS[@]}"
