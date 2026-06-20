#!/bin/bash
set -e
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
export BINARIES_DIR="/home/berk/BotOS/buildroot-engine/output/images"
export PATH="/home/berk/BotOS/buildroot-engine/output/host/bin:/home/berk/BotOS/buildroot-engine/output/host/sbin:$PATH"
export BUILD_DIR="/home/berk/BotOS/buildroot-engine/output/build"
export BR2_CONFIG="/home/berk/BotOS/buildroot-engine/.config"

echo "Sanitized PATH: $PATH"
echo "Binaries Dir: $BINARIES_DIR"
echo "Build Dir: $BUILD_DIR"
echo "BR2_CONFIG: $BR2_CONFIG"

cd /home/berk/BotOS/buildroot-engine

# Copy the correct grub-efi.cfg template to the efi-part first
echo "Installing correct GRUB config..."
cp board/pc/grub-efi.cfg output/images/efi-part/EFI/BOOT/grub.cfg

echo "Running post-image-efi.sh..."
bash -x ./board/pc/post-image-efi.sh
echo "Finished!"
