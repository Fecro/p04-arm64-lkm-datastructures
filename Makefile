# Master Makefile for p04-arm64-lkm-datastructures
# Orchestrates automatic toolchain bootstrap, LKM build, Modern C23 test suite,
# LSP generation, checkpatch validation, QEMU execution, and GDB debugging.

ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_VER    ?= 6.18.65

ROOT_DIR      := $(CURDIR)
KERNEL_SRC    := $(ROOT_DIR)/src/linux-$(KERNEL_VER)
KERNEL_BUILD  := $(ROOT_DIR)/build/kernel
BUILD_DIR     := $(ROOT_DIR)/build
ROOTFS_DIR    := $(BUILD_DIR)/rootfs
INITRAMFS     := $(BUILD_DIR)/initramfs.cpio.gz
KERNEL_IMG    := $(KERNEL_BUILD)/arch/arm64/boot/Image
BUSYBOX_BIN   := $(ROOTFS_DIR)/bin/busybox

MODULE_DIR    := $(ROOT_DIR)/module
MODULE_KO     := $(MODULE_DIR)/kds_store.ko
MODULE_SRCS   := $(MODULE_DIR)/kds_store.h \
                 $(MODULE_DIR)/kds_core.c \
                 $(MODULE_DIR)/kds_main.c \
                 $(MODULE_DIR)/kds_procfs.c \
                 $(MODULE_DIR)/kds_sysfs.c

USER_DIR      := $(ROOT_DIR)/userspace
USER_BIN      := $(USER_DIR)/bin/kds_tester

.PHONY: help bootstrap all module userspace initramfs lsp check run debug gdb demo \
        clean-module clean-userspace clean distclean

help:
	@echo "p04-arm64-lkm-datastructures - Master Build & Verification Targets"
	@echo "  make bootstrap  - Download and build Linux $(KERNEL_VER) & static BusyBox"
	@echo "  make all        - Build LKM, C23 test suite, and initramfs"
	@echo "  make module     - Build kds_store.ko and update initramfs"
	@echo "  make userspace  - Build C23 kds_tester (aarch64 static) and update initramfs"
	@echo "  make initramfs  - Repackage build/initramfs.cpio.gz"
	@echo "  make check      - Run kernel checkpatch.pl and full -Werror build"
	@echo "  make demo       - Run unattended end-to-end QEMU test & kmemleak demo"
	@echo "  make run        - Boot interactive QEMU arm64 virt shell"
	@echo "  make debug      - Boot QEMU paused, waiting for GDB on tcp::1234"
	@echo "  make gdb        - Attach gdb-multiarch with vmlinux-gdb.py to QEMU"
	@echo "  make lsp        - Generate compile_commands.json for module/ and userspace/"
	@echo "  make clean      - Clean LKM and userspace build outputs (keeps kernel/BusyBox)"
	@echo "  make distclean  - Clean everything including build/"

bootstrap: $(KERNEL_IMG) $(BUSYBOX_BIN)

$(KERNEL_IMG) $(BUSYBOX_BIN):
	@ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) KERNEL_VER=$(KERNEL_VER) \
		$(ROOT_DIR)/scripts/bootstrap.sh

all: module userspace

module: $(KERNEL_IMG)
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

initramfs: $(BUSYBOX_BIN)
	@mkdir -p $(ROOTFS_DIR)/proc $(ROOTFS_DIR)/sys $(ROOTFS_DIR)/dev \
		$(ROOTFS_DIR)/etc $(ROOTFS_DIR)/tmp $(ROOTFS_DIR)/root \
		$(ROOTFS_DIR)/lib/modules $(ROOTFS_DIR)/usr/bin
	@cp $(ROOT_DIR)/rootfs/init $(ROOTFS_DIR)/init
	@cp $(ROOT_DIR)/rootfs/kds_memcheck.sh $(ROOTFS_DIR)/usr/bin/kds_memcheck.sh
	@chmod +x $(ROOTFS_DIR)/init $(ROOTFS_DIR)/usr/bin/kds_memcheck.sh
	@cd $(ROOTFS_DIR) && \
		find . -print0 | \
		cpio --null -ov --format=newc --owner=+0:+0 2>/dev/null | \
		gzip -9 > $(INITRAMFS)
	@echo "[OK] Built $(INITRAMFS)"

lsp: $(KERNEL_IMG)
	@python3 $(KERNEL_SRC)/scripts/clang-tools/gen_compile_commands.py \
		-d $(KERNEL_BUILD) \
		-o $(MODULE_DIR)/compile_commands.json \
		$(MODULE_DIR)
	@cd $(USER_DIR) && bear -- $(MAKE) clean all CROSS_COMPILE=$(CROSS_COMPILE) >/dev/null
	@echo "[OK] Generated compile_commands.json for module/ and userspace/"

check: $(KERNEL_IMG)
	@echo "[CHECK] Running Linux kernel checkpatch.pl..."
	@$(KERNEL_SRC)/scripts/checkpatch.pl --no-tree -f $(MODULE_SRCS)
	@$(MAKE) all
	@echo "[OK] Style check and full build succeeded"

run: all
	@$(ROOT_DIR)/scripts/run_qemu.sh

debug: all
	@$(ROOT_DIR)/scripts/run_qemu.sh --debug

gdb:
	@$(ROOT_DIR)/scripts/attach_gdb.sh

demo: all
	@$(ROOT_DIR)/scripts/run_qemu.sh --demo

clean-module:
	@$(MAKE) -C $(MODULE_DIR) KDIR=$(KERNEL_BUILD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

clean-userspace:
	@$(MAKE) -C $(USER_DIR) clean

clean: clean-module clean-userspace

distclean: clean
	rm -rf $(BUILD_DIR)
