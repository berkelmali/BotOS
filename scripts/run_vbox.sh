#!/usr/bin/env bash
# ============================================================
# BotOS Core — VirtualBox Launch Script
# ============================================================
# Creates and launches a BotOS virtual machine in VirtualBox.
#
# Usage:
#   ./scripts/run_vbox.sh [OPTIONS]
#
# Options:
#   --create        Create a new VM (default: start existing)
#   --delete        Delete the existing BotOS VM
#   --memory N      RAM in MB (default: 512)
#   --cpus N        Number of CPU cores (default: 2)
#   --disk PATH     Path to the disk image (.vdi)
#   --help          Show this help message
#
# Prerequisites:
#   - VirtualBox installed with VBoxManage in PATH
#   - A .vdi disk image (convert from raw with VBoxManage)
# ============================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Defaults ───────────────────────────────────────────────
VM_NAME="BotOS-Core"
MEMORY="512"
CPUS="2"
DISK="${PROJECT_ROOT}/buildroot/output/images/rootfs.vdi"
ACTION="start"

# ── Colors ─────────────────────────────────────────────────
CYAN='\033[0;36m'
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${CYAN}[VBox]${NC} $*"; }
log_err()  { echo -e "${RED}[VBox]${NC} $*"; }
log_ok()   { echo -e "${GREEN}[VBox]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[VBox]${NC} $*"; }

# ── Argument Parsing ───────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --create)  ACTION="create"; shift ;;
        --delete)  ACTION="delete"; shift ;;
        --memory)  MEMORY="$2"; shift 2 ;;
        --cpus)    CPUS="$2"; shift 2 ;;
        --disk)    DISK="$2"; shift 2 ;;
        --help)
            head -n 20 "$0" | tail -n +3 | sed 's/^# //' | sed 's/^#//'
            exit 0
            ;;
        *)
            log_err "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ── Check VBoxManage ───────────────────────────────────────
if ! command -v VBoxManage &>/dev/null; then
    log_err "VBoxManage not found. Please install VirtualBox."
    exit 1
fi

# ── Actions ────────────────────────────────────────────────
create_vm() {
    log_info "Creating VM: ${VM_NAME}"

    # Convert raw image to VDI if needed
    if [[ ! -f "${DISK}" ]]; then
        RAW_IMAGE="${PROJECT_ROOT}/buildroot/output/images/rootfs.ext4"
        if [[ -f "${RAW_IMAGE}" ]]; then
            log_info "Converting raw image to VDI..."
            VBoxManage convertfromraw "${RAW_IMAGE}" "${DISK}" --format VDI
        else
            log_err "No disk image found. Build the system first."
            exit 1
        fi
    fi

    # Create and configure VM
    VBoxManage createvm --name "${VM_NAME}" --ostype Linux_64 --register

    VBoxManage modifyvm "${VM_NAME}" \
        --memory "${MEMORY}" \
        --cpus "${CPUS}" \
        --vram 32 \
        --graphicscontroller vmsvga \
        --audio-driver none \
        --nic1 nat \
        --natpf1 "ssh,tcp,,2222,,22" \
        --uart1 0x3F8 4 \
        --uartmode1 file "${PROJECT_ROOT}/build/serial.log" \
        --boot1 disk \
        --boot2 none \
        --boot3 none \
        --boot4 none

    # Attach storage
    VBoxManage storagectl "${VM_NAME}" --name "SATA" --add sata --controller IntelAhci
    VBoxManage storageattach "${VM_NAME}" \
        --storagectl "SATA" \
        --port 0 --device 0 \
        --type hdd --medium "${DISK}"

    log_ok "VM '${VM_NAME}' created successfully."
}

delete_vm() {
    log_warn "Deleting VM: ${VM_NAME}"
    VBoxManage unregistervm "${VM_NAME}" --delete 2>/dev/null || true
    log_ok "VM deleted."
}

start_vm() {
    log_info "Starting VM: ${VM_NAME}"

    if ! VBoxManage showvminfo "${VM_NAME}" &>/dev/null; then
        log_err "VM '${VM_NAME}' not found. Create it first with --create"
        exit 1
    fi

    VBoxManage startvm "${VM_NAME}" --type headless
    log_ok "VM started in headless mode."
    log_info "SSH access: ssh -p 2222 root@localhost"
    log_info "Serial log: ${PROJECT_ROOT}/build/serial.log"
}

# ── Execute ────────────────────────────────────────────────
echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║     BotOS Core — VirtualBox Manager      ║"
echo "  ╚══════════════════════════════════════════╝"
echo ""

case "${ACTION}" in
    create) create_vm ;;
    delete) delete_vm ;;
    start)  start_vm ;;
esac
