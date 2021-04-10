
BUILD_DIR = ../../bin

ASM = nasm
ASM_FLAGS = -f bin

mbr: boot_loader.asm
	make clean
	${ASM} ${ASM_FLAGS} -o bin/mbr.bin $^
	cp bin/mbr.bin ${BUILD_DIR}/mbr.bin
	qemu-system-x86_64 bin/mbr.bin

clean:
	rm -rf bin
	mkdir bin