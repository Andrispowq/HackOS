
BUILD_DIR = ../../../bin

ASM = nasm
ASM_FLAGS = -f bin

bootloader: boot_loader.asm
	make clean
	${ASM} ${ASM_FLAGS} -o bin/bootloader.bin $^
	cp bin/bootloader.bin ${BUILD_DIR}/bootloader.bin

clean:
	rm -rf bin
	mkdir bin