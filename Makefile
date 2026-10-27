.DEFAULT_GOAL := build
CROSS_COMPILE ?= x86_64-elf-
export CROSS_COMPILE
TARGET_BIOS ?= true
FIRMWARE := $(if $(filter true,$(TARGET_BIOS)),bios,uefi)
PYTHON ?= python3

.PHONY: build all init build_bios build_efi bios uefi kernel userspace buildimg run debug clean
build: $(if $(filter true,$(TARGET_BIOS)),build_bios,build_efi)
init: build
all: build
	$(MAKE) run

build_bios: bios kernel userspace
	$(PYTHON) tools/make_image.py --firmware bios
build_efi: uefi kernel userspace
	$(PYTHON) tools/make_image.py --firmware uefi
bios:
	$(MAKE) -C boot/BIOS/mbr mbr
	$(MAKE) -C boot/BIOS/stage_1 bootloader
	$(MAKE) -C boot/BIOS/stage_2 second_stage
uefi:
	$(MAKE) -C boot/UEFI/gnu-efi ARCH=x86_64 lib
	$(MAKE) -C boot/UEFI/gnu-efi ARCH=x86_64 gnuefi
	$(MAKE) -C boot/UEFI/gnu-efi ARCH=x86_64 bootloader
kernel:
	$(MAKE) -C kernel kernel
userspace:
	$(MAKE) -C libs/libc
	$(MAKE) -C libs/shell
	$(MAKE) -C libs/commands
	$(MAKE) -C libs/test
	$(MAKE) -C libs/input
	$(MAKE) -C libs/window
	$(MAKE) -C libs/ring3
buildimg:
	$(PYTHON) tools/make_image.py --firmware $(FIRMWARE)
run:
	$(PYTHON) tools/run_qemu.py --firmware $(FIRMWARE) --image bin/HackOS.img
# GDB listens only on loopback; attach separately to localhost:1234.
debug:
	$(PYTHON) tools/run_qemu.py --firmware $(FIRMWARE) --image bin/HackOS.img --debug
clean:
	rm -rf bin
	$(MAKE) -C kernel clean
	$(MAKE) -C boot/BIOS/mbr clean
	$(MAKE) -C boot/BIOS/stage_1 clean
	$(MAKE) -C boot/BIOS/stage_2 clean
	$(MAKE) -C libs/libc clean
	$(MAKE) -C libs/shell clean
	$(MAKE) -C libs/commands clean
	$(MAKE) -C libs/test clean
	$(MAKE) -C libs/input clean
	$(MAKE) -C libs/window clean
	$(MAKE) -C libs/ring3 clean
	rm -rf boot/UEFI/gnu-efi/x86_64
