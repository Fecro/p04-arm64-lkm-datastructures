#!/usr/bin/env bash
# Automatically download, configure, and build Linux 6.18.65 (arm64)
# and static BusyBox 1.37.0 if not already present in build/.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${ARCH:-arm64}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
KERNEL_VER="${KERNEL_VER:-6.18.65}"
BUSYBOX_VER="${BUSYBOX_VER:-1.37.0}"
JOBS="${JOBS:-$(nproc)}"

DL_DIR="${ROOT_DIR}/downloads"
SRC_DIR="${ROOT_DIR}/src"
BUILD_DIR="${ROOT_DIR}/build"
KERNEL_SRC="${SRC_DIR}/linux-${KERNEL_VER}"
KERNEL_BUILD="${BUILD_DIR}/kernel"
KERNEL_IMG="${KERNEL_BUILD}/arch/arm64/boot/Image"
BUSYBOX_SRC="${SRC_DIR}/busybox-${BUSYBOX_VER}"
BUSYBOX_BUILD="${BUILD_DIR}/busybox"
ROOTFS_DIR="${BUILD_DIR}/rootfs"
BUSYBOX_BIN="${ROOTFS_DIR}/bin/busybox"

mkdir -p "${DL_DIR}" "${SRC_DIR}" "${KERNEL_BUILD}" "${BUSYBOX_BUILD}" "${ROOTFS_DIR}"

# ---- 1. Linux Kernel (arm64 + DWARF5 + GDB scripts + kmemleak) ----
if [[ ! -d "${KERNEL_SRC}" ]]; then
  TARBALL="${DL_DIR}/linux-${KERNEL_VER}.tar.xz"
  if [[ ! -f "${TARBALL}" ]]; then
    echo "[BOOTSTRAP] Downloading Linux ${KERNEL_VER}..."
    curl -fL -o "${TARBALL}" \
      "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VER}.tar.xz"
  fi
  echo "[BOOTSTRAP] Extracting Linux ${KERNEL_VER}..."
  tar -xf "${TARBALL}" -C "${SRC_DIR}/"
fi

if [[ ! -f "${KERNEL_IMG}" || ! -f "${KERNEL_BUILD}/vmlinux-gdb.py" ]]; then
  echo "[BOOTSTRAP] Configuring Linux ${KERNEL_VER} (${ARCH})..."
  make -C "${KERNEL_SRC}" \
    O="${KERNEL_BUILD}" \
    ARCH="${ARCH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    defconfig

  (cd "${KERNEL_SRC}" &&
    ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" \
      ./scripts/kconfig/merge_config.sh \
      -O "${KERNEL_BUILD}" \
      "${KERNEL_BUILD}/.config" \
      "${ROOT_DIR}/kernel/debug_lkm.fragment")

  echo "[BOOTSTRAP] Building Linux ${KERNEL_VER} (Image, modules, scripts_gdb)..."
  make -C "${KERNEL_SRC}" \
    O="${KERNEL_BUILD}" \
    ARCH="${ARCH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    -j"${JOBS}" \
    Image modules scripts_gdb
fi

# ---- 2. Static BusyBox for arm64 initramfs ----
if [[ ! -d "${BUSYBOX_SRC}" ]]; then
  BB_TAR="${DL_DIR}/busybox-${BUSYBOX_VER}.tar.bz2"
  if [[ ! -f "${BB_TAR}" ]]; then
    echo "[BOOTSTRAP] Downloading BusyBox ${BUSYBOX_VER}..."
    curl -fL -o "${BB_TAR}" \
      "https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
  fi
  echo "[BOOTSTRAP] Extracting BusyBox ${BUSYBOX_VER}..."
  tar -xf "${BB_TAR}" -C "${SRC_DIR}/"
fi

if [[ ! -f "${BUSYBOX_BIN}" ]]; then
  echo "[BOOTSTRAP] Configuring and building static BusyBox ${BUSYBOX_VER}..."
  make -C "${BUSYBOX_SRC}" \
    O="${BUSYBOX_BUILD}" \
    ARCH="${ARCH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    defconfig

  sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "${BUSYBOX_BUILD}/.config"
  sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' "${BUSYBOX_BUILD}/.config"
  sed -i 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' "${BUSYBOX_BUILD}/.config"
  sed -i 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' "${BUSYBOX_BUILD}/.config"

  make -C "${BUSYBOX_SRC}" \
    O="${BUSYBOX_BUILD}" \
    ARCH="${ARCH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    -j"${JOBS}"

  make -C "${BUSYBOX_SRC}" \
    O="${BUSYBOX_BUILD}" \
    ARCH="${ARCH}" \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    CONFIG_PREFIX="${ROOTFS_DIR}" \
    install
fi

echo "[BOOTSTRAP] Kernel and BusyBox ready."
