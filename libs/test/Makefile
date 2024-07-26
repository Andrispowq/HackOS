
SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := bin-int

PROGRAM_NAME := test

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRC = $(call rwildcard,$(SRC_DIR),*.cpp)  
ASM_SRC = $(call rwildcard,$(SRC_DIR),*.asm)  
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRC))
OBJS += $(patsubst $(SRC_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_SRC))
DIRS = $(wildcard $(SRC_DIR)/*)

CC = gcc
ASM = nasm
LD = ld

LDS = userspace.ld
CFLAGS = -m64 -c -g -I ../libc/src
ASM_FLAGS = -f elf64
LDFLAGS = -T $(LDS) -static -Bsymbolic -nostdlib

all: $(OBJS) link

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $^ -o $@
	
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm
	@ echo !==== COMPILING $^
	@ mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) $^ -o $@

link:
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o $(BIN_DIR)/$(PROGRAM_NAME) $(OBJS) ../libc/bin/libc.a

# cleaning
clean:
	rm -rf ${OBJ_DIR}
	rm -rf ${BIN_DIR}
	mkdir ${OBJ_DIR}
	mkdir ${BIN_DIR}