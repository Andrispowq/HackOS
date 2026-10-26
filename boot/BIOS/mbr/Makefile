BUILD_DIR = ../../../bin
ASM = nasm
.PHONY: mbr clean
mbr: $(BUILD_DIR)/mbr.bin
$(BUILD_DIR)/mbr.bin: boot_loader.asm
	mkdir -p bin $(BUILD_DIR)
	$(ASM) -f bin -o bin/mbr.bin $<
	cp bin/mbr.bin $@
clean:
	rm -rf bin
