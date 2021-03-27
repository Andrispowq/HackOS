SRC_DIR := src
BIN_DIR := bin
BUILD_DIR := ../../bin
OBJ_DIR := bin-int

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRC = $(call rwildcard,$(SRC_DIR),*.c)
ASM_SRC = $(call rwildcard,$(SRC_DIR),*.asm)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))
OBJS += $(patsubst $(SRC_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_SRC))
DIRS = $(wildcard $(SRC_DIR)/*)

CC = gcc
ASM = nasm
LD = ld

LDS = link.ld
CFLAGS = -m64 -ffreestanding -fno-pic -c -g -I src
ASM_FLAGS = -f elf64 -g
LDFLAGS = -T $(LDS)

second_stage: $(OBJS) link

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $^ -o $@
	
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) $^ -o $@

link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BIN_DIR)/second_stage.elf $(OBJS)
	objcopy --strip-debug $(BIN_DIR)/second_stage.elf --output-format=binary $(BUILD_DIR)/second_stage.bin

# cleaning
clean:
	rm -rf ${OBJ_DIR}
	rm -rf ${BIN_DIR}
	mkdir ${OBJ_DIR}
	mkdir ${BIN_DIR}