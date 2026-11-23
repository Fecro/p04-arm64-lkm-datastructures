# Master Makefile for p04-arm64-lkm-datastructures
# Orchestrates initramfs packaging and QEMU arm64 virt execution.

ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
ROOT_DIR      := $(CURDIR)
BUILD_DIR     := $(ROOT_DIR)/build
ROOTFS_DIR    := $(BUILD_DIR)/rootfs
INITRAMFS     := $(BUILD_DIR)/initramfs.cpio.gz

.PHONY: help initramfs run debug

help:
	@echo "Available targets:"
	@echo "  make initramfs  - Rebuild initramfs.cpio.gz from build/rootfs and rootfs/init"
	@echo "  make run        - Boot Linux 6.18.65 arm64 in QEMU virt"
	@echo "  make debug      - Boot QEMU paused, waiting for GDB on tcp::1234"

initramfs:
	@mkdir -p $(ROOTFS_DIR)/{proc,sys,dev,etc,tmp,root,lib/modules}
	@cp $(ROOT_DIR)/rootfs/init $(ROOTFS_DIR)/init
	@chmod +x $(ROOTFS_DIR)/init
	@cd $(ROOTFS_DIR) && \
		find . -print0 | \
		cpio --null -ov --format=newc --owner=+0:+0 2>/dev/null | \
		gzip -9 > $(INITRAMFS)
	@echo "[OK] Built $(INITRAMFS)"

run:
	@$(ROOT_DIR)/scripts/run_qemu.sh

debug:
	@$(ROOT_DIR)/scripts/run_qemu.sh --debug
