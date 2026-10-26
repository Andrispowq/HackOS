
SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := bin-int

PROGRAM_NAME := shell

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

LDS = userspace.ld
CFLAGS = -std=gnu++17 -fno-pie -fno-stack-protector -fno-omit-frame-pointer -MMD -MP -m64 -ffreestanding -mno-red-zone -fno-rtti -fno-exceptions -c -g -I ../libc/src
ASM_FLAGS = -f elf64
LDFLAGS = -T $(LDS) -static -Bsymbolic -nostdlib

.PHONY: all link clean
all: $(BIN_DIR)/$(PROGRAM_NAME)
link: $(BIN_DIR)/$(PROGRAM_NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp Makefile
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
	
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm $(call rwildcard,$(SRC_DIR),*.inc)
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) -MD $(@:.o=.d) $< -o $@

$(BIN_DIR)/$(PROGRAM_NAME): $(OBJS) $(LDS) ../libc/bin/libc.a
	@ mkdir -p $(@D)
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BIN_DIR)/$(PROGRAM_NAME) $(OBJS) ../libc/bin/libc.a

# cleaning
clean:
	rm -rf ${OBJ_DIR}
	rm -rf ${BIN_DIR}
	mkdir ${OBJ_DIR}
	mkdir ${BIN_DIR}
-include $(OBJS:.o=.d)

../libc/bin/libc.a:
	$(MAKE) -C ../libc
