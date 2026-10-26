CROSS_COMPILE ?= x86_64-elf-
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
ASM = nasm
CFLAGS = -std=gnu++17 -O2 -g -ffreestanding -mno-red-zone -mcmodel=large -fno-pie -fno-stack-protector -fno-omit-frame-pointer -fno-exceptions -fno-rtti -MMD -MP -I ../../include
.DEFAULT_GOAL := all
.PHONY: all clean
all: bin/ring3
bin-int/main.o: src/main.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin-int/syscall_entry.o: ../../common/src/x86_64/syscall_entry.asm Makefile
	@mkdir -p $(@D)
	$(ASM) -f elf64 -MD $(@:.o=.d) $< -o $@
bin/ring3: bin-int/main.o bin-int/syscall_entry.o userspace.ld
	@mkdir -p $(@D)
	$(LD) -T userspace.ld -static -nostdlib -o $@ bin-int/main.o bin-int/syscall_entry.o
clean:
	rm -rf bin bin-int
-include bin-int/*.d
