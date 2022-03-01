
OS_NAME = HackOS

BOOTLOADER_DIR = boot

KERNEL_DIR = kernel
BUILD_DIR := bin

TARGET_BIOS = true

ifneq ($(TARGET_BIOS), true)

GNU_EFI = $(BOOTLOADER_DIR)/UEFI/gnu-efi
OVMF_DIR = $(BOOTLOADER_DIR)/UEFI/OVMFbin

BOOT_EFI := $(GNU_EFI)/x86_64/bootloader/main.efi

init:
	make clean
	make -C boot/UEFI/gnu-efi

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
	mmd -i $(BUILD_DIR)/$(OS_NAME).img ::/USR
	mmd -i $(BUILD_DIR)/$(OS_NAME).img ::/USR/BIN
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(BOOT_EFI) ::/EFI/BOOT
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(KERNEL_DIR)/startup.nsh ::
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img $(KERNEL_DIR)/bin/kernel.elf ::
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img util/fonts/zap-light16.psf ::
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img util/usertest/usertest.elf ::/USR/BIN
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img hello.txt ::/USR
	mcopy -i $(BUILD_DIR)/$(OS_NAME).img util/libc/test ::/USR/BIN

run:
	qemu-system-x86_64 \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img \
	-m 1G \
	-cpu qemu64 \
	-machine q35 \
	-smp cores=4,threads=1,sockets=1 \
	-drive if=pflash,format=raw,unit=0,file="$(OVMF_DIR)/OVMF_CODE-pure-efi.fd",readonly=on \
	-drive if=pflash,format=raw,unit=1,file="$(OVMF_DIR)/OVMF_VARS-pure-efi.fd" \
	-usb \
	-device usb-ehci,id=ehci \
	-device qemu-xhci,id=xhci \
	-net none \

debug:
	qemu-system-x86_64 \
	-s \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img \
	-m 1G \
	-cpu qemu64 \
	-machine q35 \
	-smp cores=4,threads=1,sockets=1 \
	-drive if=pflash,format=raw,unit=0,file="$(OVMF_DIR)/OVMF_CODE-pure-efi.fd",readonly=on \
	-drive if=pflash,format=raw,unit=1,file="$(OVMF_DIR)/OVMF_VARS-pure-efi.fd" \
	-usb \
	-device usb-ehci,id=ehci \
	-device qemu-xhci,id=xhci \
	-net none \
	-d guest_errors,cpu_reset,int \
	-no-reboot -no-shutdown & \
	gdb -ex "target remote localhost:1234" -ex "symbol-file kernel/bin/kernel.elf" \

clean:
	make -C kernel clean
	rm -rf $(GNU_EFI)/x86_64/bootloader

else

MBR_DIR = $(BOOTLOADER_DIR)/BIOS/mbr
FIRST_STAGE_DIR = $(BOOTLOADER_DIR)/BIOS/stage_1
SECOND_STAGE_DIR = $(BOOTLOADER_DIR)/BIOS/stage_2

init:
	make clean
	make -C boot/UEFI/gnu-efi

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
	mformat -i $(BUILD_DIR)/HackOS_FAT.img -F -B bin/bootloader.bin ::
	mmd -i $(BUILD_DIR)/HackOS_FAT.img ::/SYSTEM
	mmd -i $(BUILD_DIR)/HackOS_FAT.img ::/USR
	mmd -i $(BUILD_DIR)/HackOS_FAT.img ::/USR/BIN
	mcopy -i $(BUILD_DIR)/HackOS_FAT.img util/usertest/usertest.elf ::/USR/BIN
	mcopy -i $(BUILD_DIR)/HackOS_FAT.img hello.txt ::/USR
	mcopy -i $(BUILD_DIR)/HackOS_FAT.img util/libc/test ::/USR/BIN

	dd if=$(BUILD_DIR)/mbr.bin of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=0 count=1
	dd if=$(BUILD_DIR)/bootloader.bin of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=1 count=1
	dd if=$(BUILD_DIR)/second_stage.bin of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=2 count=64
	dd if=util/fonts/zap-light16.psf of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=66 count=12
	dd if=$(BUILD_DIR)/kernel.elf of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=78 count=256
	dd if=$(BUILD_DIR)/HackOS_FAT.img of=$(BUILD_DIR)/$(OS_NAME).img bs=512 seek=334 count=93750
	
run:
	qemu-system-x86_64 \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img,format=raw \
	-m 1G \
	-cpu qemu64 \
	-machine q35 \
	-smp cores=4,threads=1,sockets=1 \
	-usb \
	-device usb-ehci,id=ehci \
	-device qemu-xhci,id=xhci \
	
	-net none \
	-rtc clock=host,base=localtime \

# ,if=none,id=stick
# -device usb-storage,bus=xhci.0,drive=stick \

install:
	sudo dd if=bin/HackOS.img of=/dev/sdb

debug:
	qemu-system-x86_64 \
	-s \
	-drive file=$(BUILD_DIR)/$(OS_NAME).img,format=raw \
	-m 1G \
	-cpu qemu64 \
	-machine q35 \
	-smp cores=4,threads=1,sockets=1 \
	-usb \
	-device usb-ehci,id=ehci \
	-device qemu-xhci,id=xhci \
	-net none \
	-d guest_errors,cpu_reset & \
	gdb -ex "target remote localhost:1234" -ex "symbol-file kernel/bin/kernel.elf" \

# boot/BIOS/stage_2/bin/second_stage.elf
clean:
	rm -rf $(BUILD_DIR)
	mkdir $(BUILD_DIR)
	make -C $(KERNEL_DIR) clean
	make -C $(SECOND_STAGE_DIR) clean
	make -C $(FIRST_STAGE_DIR) clean
	make -C $(MBR_DIR) clean

endif
