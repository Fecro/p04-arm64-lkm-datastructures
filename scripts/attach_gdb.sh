#!/usr/bin/env bash
# Attach gdb-multiarch to a paused QEMU arm64 virt instance (-s -S)
# with Linux 6.18.65 vmlinux DWARF5 symbols and kernel GDB helper scripts.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_BUILD="${ROOT_DIR}/build/kernel"
VMLINUX="${KERNEL_BUILD}/vmlinux"
MODULE_DIR="${ROOT_DIR}/module"

if [[ ! -f "${VMLINUX}" ]]; then
  echo "Error: vmlinux not found at ${VMLINUX}" >&2
  exit 1
fi

echo "[GDB] Attaching to QEMU on localhost:1234..."
echo "[GDB] After booting and running 'insmod /lib/modules/kds_store.ko' in QEMU,"
echo "      press Ctrl+C here and run: lx-symbols ${MODULE_DIR}"
echo ""

exec gdb-multiarch \
  -iex "add-auto-load-safe-path ${KERNEL_BUILD}" \
  "${VMLINUX}" \
  -ex "target remote :1234"
