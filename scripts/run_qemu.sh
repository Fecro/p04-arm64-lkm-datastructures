#!/usr/bin/env bash
# Launch QEMU arm64 virt machine with Linux 6.18.65 and minimal initramfs.
# Usage:
#   ./scripts/run_qemu.sh          # Normal interactive boot
#   ./scripts/run_qemu.sh --debug  # Wait for GDB connection on tcp::1234 (-s -S)
#   ./scripts/run_qemu.sh --demo   # Unattended automated test & poweroff

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

CMDLINE="console=ttyAMA0 rdinit=/init nokaslr kmemleak=on loglevel=7"
QEMU_EXTRA_ARGS=()

case "${1:-}" in
--debug)
  echo "[QEMU] Waiting for GDB on tcp::1234 (use 'make gdb' in another terminal)..."
  QEMU_EXTRA_ARGS+=(-s -S)
  ;;
--demo)
  echo "[QEMU] Starting unattended automated portfolio demo..."
  CMDLINE="${CMDLINE} kds_demo=1"
  QEMU_EXTRA_ARGS+=(-no-reboot)
  ;;
"")
  ;;
*)
  echo "Usage: $0 [--debug|--demo]" >&2
  exit 1
  ;;
esac

exec qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a53 \
  -smp 2 \
  -m 512M \
  -nographic \
  -audio none \
  -kernel "${KERNEL_IMG}" \
  -initrd "${INITRAMFS}" \
  -append "${CMDLINE}" \
  "${QEMU_EXTRA_ARGS[@]}"
