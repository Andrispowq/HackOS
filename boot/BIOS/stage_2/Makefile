SRC_DIR := src
BIN_DIR := bin
BUILD_DIR := ../../../bin
OBJ_DIR := bin-int

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRC = $(call rwildcard,$(SRC_DIR),*.c)
ASM_SRC = $(call rwildcard,$(SRC_DIR),*.asm)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))
OBJS += $(patsubst $(SRC_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_SRC))
DIRS = $(wildcard $(SRC_DIR)/*)

CROSS_COMPILE ?= x86_64-elf-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
ASM = nasm
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar
OBJCOPY = $(CROSS_COMPILE)objcopy

LDS = link.ld
CFLAGS = -std=gnu11 -fno-pie -fno-stack-protector -fno-omit-frame-pointer -MMD -MP -m64 -mno-red-zone -ffreestanding -fno-pic -c -g -I src
ASM_FLAGS = -f elf64 -g
LDFLAGS = -T $(LDS)

.PHONY: second_stage link clean
second_stage: $(BUILD_DIR)/second_stage.bin
link: $(BIN_DIR)/second_stage.elf

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c Makefile
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm $(call rwildcard,$(SRC_DIR),*.inc)
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) -MD $(@:.o=.d) $< -o $@

$(BIN_DIR)/second_stage.elf: $(OBJS) $(LDS)
	@ mkdir -p $(@D)
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BIN_DIR)/second_stage.elf $(OBJS)

$(BUILD_DIR)/second_stage.bin: $(BIN_DIR)/second_stage.elf
	@ mkdir -p $(@D)
	$(OBJCOPY) --strip-all --output-format=binary $< $@

# cleaning
clean:
	rm -rf ${OBJ_DIR}
	rm -rf ${BIN_DIR}
	mkdir ${OBJ_DIR}
	mkdir ${BIN_DIR}
-include $(OBJS:.o=.d)
