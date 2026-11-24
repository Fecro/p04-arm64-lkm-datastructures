# Master Makefile for p04-arm64-lkm-datastructures
# Orchestrates LKM build, initramfs packaging, LSP database, and QEMU execution.

ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_VER    ?= 6.18.65
ROOT_DIR      := $(CURDIR)
KERNEL_SRC    := $(ROOT_DIR)/src/linux-$(KERNEL_VER)
KERNEL_BUILD  := $(ROOT_DIR)/build/kernel
BUILD_DIR     := $(ROOT_DIR)/build
ROOTFS_DIR    := $(BUILD_DIR)/rootfs
INITRAMFS     := $(BUILD_DIR)/initramfs.cpio.gz
MODULE_DIR    := $(ROOT_DIR)/module
MODULE_KO     := $(MODULE_DIR)/kds_store.ko
USER_DIR      := $(ROOT_DIR)/userspace
USER_BIN      := $(USER_DIR)/bin/kds_tester

.PHONY: help module lsp initramfs run debug clean-module userspace clean-userspace

help:
	@echo "Available targets:"
	@echo "  make module        - Build kds_store.ko, install into rootfs, and rebuild initramfs"
	@echo "  make lsp           - Generate module/compile_commands.json for clangd/LazyVim"
	@echo "  make initramfs     - Rebuild initramfs.cpio.gz"
	@echo "  make run           - Boot Linux $(KERNEL_VER) arm64 in QEMU virt"
	@echo "  make debug         - Boot QEMU paused, waiting for GDB on tcp::1234"
	@echo "  make clean-module  - Clean out-of-tree LKM build artifacts"

module:
	@$(MAKE) -C $(MODULE_DIR) KDIR=$(KERNEL_BUILD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) all
	@mkdir -p $(ROOTFS_DIR)/lib/modules
	@cp $(MODULE_KO) $(ROOTFS_DIR)/lib/modules/
	@$(MAKE) initramfs
	@echo "[OK] Installed $(MODULE_KO) into $(INITRAMFS)"

userspace:
	@$(MAKE) -C $(USER_DIR) CROSS_COMPILE=$(CROSS_COMPILE) all
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@cp $(USER_BIN) $(ROOTFS_DIR)/usr/bin/kds_tester
	@$(MAKE) initramfs
	@echo "[OK] Installed $(USER_BIN) into $(INITRAMFS)"

lsp:
	@python3 $(KERNEL_SRC)/scripts/clang-tools/gen_compile_commands.py \
		-d $(KERNEL_BUILD) \
		-o $(MODULE_DIR)/compile_commands.json \
		$(MODULE_DIR)
	@cd $(USER_DIR) && bear -- $(MAKE) clean all CROSS_COMPILE=$(CROSS_COMPILE) >/dev/null
	@echo "[OK] Generated compile_commands.json for module/ and userspace/"

initramfs:
	@mkdir -p $(ROOTFS_DIR)/{proc,sys,dev,etc,tmp,root,lib/modules}
	@cp $(ROOT_DIR)/rootfs/init $(ROOTFS_DIR)/init
	@mkdir -p $(ROOTFS_DIR)/usr/bin
	@cp $(ROOT_DIR)/rootfs/kds_memcheck.sh $(ROOTFS_DIR)/usr/bin/kds_memcheck.sh
	@chmod +x $(ROOTFS_DIR)/usr/bin/kds_memcheck.sh
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

clean-module:
	@$(MAKE) -C $(MODULE_DIR) KDIR=$(KERNEL_BUILD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

clean-userspace:
	@$(MAKE) -C $(USER_DIR) clean
