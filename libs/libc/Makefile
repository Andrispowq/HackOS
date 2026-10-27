# The active ring-3 runtime. Old window/raw-pointer sources are not ABI-compatible
# and are deliberately excluded until they have safe user implementations.
CROSS_COMPILE ?= x86_64-elf-
CXX = $(CROSS_COMPILE)g++
AR = $(CROSS_COMPILE)ar
CC = $(CROSS_COMPILE)gcc
ASM = nasm
ASM_FLAGS = -f elf64
OBJ_DIR = bin-int
CFLAGS = -std=gnu++17 -O2 -g -ffreestanding -mno-red-zone -mcmodel=large -fno-pie -fno-stack-protector -fno-omit-frame-pointer -fno-exceptions -fno-rtti -MMD -MP -I ../../include -I ../../common/include
OBJS = bin-int/user.o bin-int/memory.o bin-int/stdio.o bin-int/string.o bin-int/start.o
COMMON_DIR := ../../common
include $(COMMON_DIR)/runtime.mk
.DEFAULT_GOAL := all
.PHONY: all clean
all: bin/libc.a
bin-int/%.o: src/%.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin-int/start.o: src/start.asm Makefile
	@mkdir -p $(@D)
	$(ASM) -f elf64 -MD $(@:.o=.d) $< -o $@
bin/libc.a: $(OBJS) Makefile
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $(OBJS)
clean:
	rm -rf bin bin-int
-include $(OBJS:.o=.d)
