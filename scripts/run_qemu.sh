#!/usr/bin/env bash
# Launch QEMU arm64 virt machine with Linux 6.18.65 and minimal initramfs.
# Usage:
#   ./scripts/run_qemu.sh          # Normal interactive boot
#   ./scripts/run_qemu.sh --debug  # Wait for GDB connection on tcp::1234 (-s -S)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_IMG="${ROOT_DIR}/build/kernel/arch/arm64/boot/Image"
INITRAMFS="${ROOT_DIR}/build/initramfs.cpio.gz"

if [[ ! -f "${KERNEL_IMG}" ]]; then
  echo "Error: Kernel image not found at ${KERNEL_IMG}" >&2
  exit 1
fi

if [[ ! -f "${INITRAMFS}" ]]; then
  echo "Error: Initramfs not found at ${INITRAMFS}" >&2
  exit 1
fi

QEMU_EXTRA_ARGS=()
if [[ "${1:-}" == "--debug" ]]; then
  echo "[QEMU] Waiting for GDB on tcp::1234 (use gdb-multiarch)..."
  QEMU_EXTRA_ARGS+=(-s -S)
fi

exec qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -smp 2 \
  -m 512M \
  -nographic \
  -audio none \
  -kernel "${KERNEL_IMG}" \
  -initrd "${INITRAMFS}" \
  -append "console=ttyAMA0 rdinit=/init nokaslr kmemleak=on loglevel=7" \
  "${QEMU_EXTRA_ARGS[@]}"
