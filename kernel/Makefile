
OS_NAME = HackOS

GNU_EFI = ../boot/gnu-efi
OVMF_DIR = ../../bin/OVMFbin

BIOS = ../boot/BIOS_legacy

SRC_DIR := src
BIN_DIR := bin
BUILD_DIR := ../bin
OBJ_DIR := bin-int

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRC = $(call rwildcard,$(SRC_DIR),*.cpp)  
ASM_SRC = $(call rwildcard,$(SRC_DIR),*.asm)  
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRC))
OBJS += $(patsubst $(SRC_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_SRC))
DIRS = $(wildcard $(SRC_DIR)/*)

CROSS_COMPILE ?= x86_64-elf-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
ASM = nasm
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar
OBJCOPY = $(CROSS_COMPILE)objcopy

LDS = link_BIOS.ld
CFLAGS = -std=gnu++17 -fno-pie -fno-stack-protector -fno-omit-frame-pointer -MMD -MP -m64 -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -c -g -I src
ASM_FLAGS = -f elf64
LDFLAGS = -T $(LDS) -static -Bsymbolic -nostdlib

.DEFAULT_GOAL := kernel
COMMON_KERNEL := true
COMMON_DIR := ../common
include $(COMMON_DIR)/runtime.mk

# Keep the window and input code inside the fixed BIOS kernel slot. Retain
# debug information, frame pointers and all freestanding/interrupt ABI flags.
$(OBJ_DIR)/drivers/screen/window.o $(OBJ_DIR)/drivers/screen/window_manager.o $(OBJ_DIR)/proc/user/windows.o $(OBJ_DIR)/drivers/mouse/mouse.o: CFLAGS += -Os

.PHONY: kernel link clean
kernel: $(BUILD_DIR)/kernel.elf
link: $(BIN_DIR)/kernel.elf

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp Makefile $(COMMON_DIR)/runtime.mk
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
	
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm $(call rwildcard,$(SRC_DIR),*.inc)
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) -MD $(@:.o=.d) $< -o $@
	 
$(BIN_DIR)/kernel.elf: $(OBJS) $(LDS)
	@ mkdir -p $(@D)
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BIN_DIR)/kernel.elf $(OBJS)

$(BUILD_DIR)/kernel.elf: $(BIN_DIR)/kernel.elf
	@ mkdir -p $(@D)
	$(OBJCOPY) --strip-all $< $@

# cleaning
clean:
	rm -rf ${OBJ_DIR}
	rm -rf ${BIN_DIR}
	mkdir ${OBJ_DIR}
	mkdir ${BIN_DIR}
-include $(OBJS:.o=.d)
