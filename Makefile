
OS_NAME = HackOS

BOOTLOADER_DIR = boot

KERNEL_DIR = kernel
BUILD_DIR := bin

TARGET_BIOS = true

ifneq ($(TARGET_BIOS), true)

GNU_EFI = $(BOOTLOADER_DIR)/UEFI/gnu-efi
OVMF_DIR = $(BOOTLOADER_DIR)/UEFI/OVMFbin

BOOT_EFI := $(GNU_EFI)/x86_64/bootloader/main.efi

all:
	make build_efi
	make run

debug_all:
	make build_efi
	make debug

build_efi:
	make -C $(GNU_EFI) bootloader
	make -C $(KERNEL_DIR) kernel
	make buildimg	

buildimg:
	dd if=/dev/zero of=$(BUILD_DIR)/$(OS_NAME).img bs=512 count=93750
	mformat -i $(BUILD_DIR)/$(OS_NAME).img -F ::
	mmd -i $(BUILD_DIR)/$(OS_NAME).img ::/EFI
	mmd -i $(BUILD_DIR)/$(OS_NAME).img ::/EFI/BOOT
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(BOOT_EFI) ::/EFI/BOOT
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(KERNEL_DIR)/startup.nsh ::
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(KERNEL_DIR)/bin/kernel.elf ::
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img util/fonts/zap-light16.psf ::

run:
	qemu-system-x86_64 \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img \
	-m 512M \
	-cpu qemu64 \
	-machine q35 \
	-drive if=pflash,format=raw,unit=0,file="$(OVMF_DIR)/OVMF_CODE-pure-efi.fd",readonly=on \
	-drive if=pflash,format=raw,unit=1,file="$(OVMF_DIR)/OVMF_VARS-pure-efi.fd" \
	-net none \

debug:
	qemu-system-x86_64 \
	-s \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img \
	-m 512M \
	-cpu qemu64 \
	-machine q35 \
	-drive if=pflash,format=raw,unit=0,file="$(OVMF_DIR)/OVMF_CODE-pure-efi.fd",readonly=on \
	-drive if=pflash,format=raw,unit=1,file="$(OVMF_DIR)/OVMF_VARS-pure-efi.fd" \
	-net none \
	-d guest_errors,cpu_reset,int \
	-no-reboot -no-shutdown & \
	gdb -ex "target remote localhost:1234" -ex "symbol-file kernel/bin/kernel.elf" \

clean:
	make -C kernel clean
	make -C boot/stage_2 clean
	rm -rf $(GNU_EFI)/x86_64/bootloader

else

MBR_DIR = $(BOOTLOADER_DIR)/BIOS/mbr
FIRST_STAGE_DIR = $(BOOTLOADER_DIR)/BIOS/stage_1
SECOND_STAGE_DIR = $(BOOTLOADER_DIR)/BIOS/stage_2

all:
	make build_bios
	make run

debug_all:
	make build_bios
	make debug

build_bios:
	make -C $(MBR_DIR) mbr
	make -C $(FIRST_STAGE_DIR) bootloader
	make -C $(SECOND_STAGE_DIR) second_stage
	make -C $(KERNEL_DIR) kernel
	make buildimg

buildimg:
	dd if=/dev/zero of=$(BUILD_DIR)/HackOS_FAT.img bs=512 count=93750
	mformat -i $(BUILD_DIR)/HackOS_FAT.img -F -R 64 -K 63 -B bin/bootloader.bin ::
	mmd -i $(BUILD_DIR)/HackOS_FAT.img ::/BOOT
	mmd -i $(BUILD_DIR)/HackOS_FAT.img ::/BOOT/SYSTEM
	mcopy -i $(BUILD_DIR)/HackOS_FAT.img $(KERNEL_DIR)/bin/kernel.elf ::/BOOT
	mcopy -i $(BUILD_DIR)/HackOS_FAT.img util/fonts/zap-light16.psf ::/BOOT/SYSTEM

	dd if=$(BUILD_DIR)/mbr.bin of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=0 count=1
	dd if=$(BUILD_DIR)/HackOS_FAT.img of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=1 count=2
	dd if=$(BUILD_DIR)/second_stage.bin of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=3 count=61
	dd if=$(BUILD_DIR)/HackOS_FAT.img of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=64 skip=63 count=93684
	
run:
	qemu-system-x86_64 \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img,format=raw \
	-cpu qemu64 \
	-m 512M \
	-net none \
	-rtc clock=host,base=localtime \

install:
	sudo dd if=bin/HackOS.bin of=/dev/sdb

debug:
	qemu-system-x86_64 \
	-s \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img,format=raw \
	-cpu qemu64 \
	-m 512M \
	-net none \
	-d guest_errors,cpu_reset & \
	gdb -ex "target remote localhost:1234" -ex "symbol-file boot/BIOS/stage_2/bin/second_stage.elf" \

clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)
	make -C $(KERNEL_DIR) clean
	make -C $(SECOND_STAGE_DIR) clean
	make -C $(FIRST_STAGE_DIR) clean
	make -C $(MBR_DIR) clean

endif
