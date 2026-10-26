BUILD_DIR = ../../../bin
ASM = nasm
.PHONY: bootloader clean
bootloader: $(BUILD_DIR)/bootloader.bin
$(BUILD_DIR)/bootloader.bin: boot_loader.asm
	mkdir -p bin $(BUILD_DIR)
	$(ASM) -f bin -o bin/bootloader.bin $<
	cp bin/bootloader.bin $@
clean:
	rm -rf bin
