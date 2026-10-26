# Included after the component's flags. Every consumer owns its own objects.
CFLAGS += -I $(COMMON_DIR)/include -I $(COMMON_DIR)/../include
ifneq ($(COMMON_NO_SPLIT),true)
COMMON_C_SRC := $(COMMON_DIR)/src/split.c
OBJS += $(patsubst $(COMMON_DIR)/src/%.c,$(OBJ_DIR)/common/%.o,$(COMMON_C_SRC))
endif

ifneq ($(COMMON_BOOT_ONLY),true)
COMMON_CPP_SRC := $(addprefix $(COMMON_DIR)/src/,memory_ops.cpp string_ops.cpp bitmap.cpp)
COMMON_ASM_SRC := $(addprefix $(COMMON_DIR)/src/x86_64/,_memory.asm _string.asm)
ifneq ($(COMMON_KERNEL),true)
COMMON_CPP_SRC += $(COMMON_DIR)/src/syscall_wrappers.cpp
COMMON_ASM_SRC += $(COMMON_DIR)/src/x86_64/syscall_entry.asm
endif
OBJS += $(patsubst $(COMMON_DIR)/src/%.cpp,$(OBJ_DIR)/common/%.o,$(COMMON_CPP_SRC))
OBJS += $(patsubst $(COMMON_DIR)/src/%.asm,$(OBJ_DIR)/common/%.o,$(COMMON_ASM_SRC))
endif

$(OBJ_DIR)/common/%.o: $(COMMON_DIR)/src/%.cpp Makefile $(COMMON_DIR)/runtime.mk
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/common/%.o: $(COMMON_DIR)/src/%.c Makefile $(COMMON_DIR)/runtime.mk
	@mkdir -p $(@D)
	$(CC) $(filter-out -std=gnu++17 -fno-rtti -fno-exceptions,$(CFLAGS)) -std=gnu11 -c $< -o $@

$(OBJ_DIR)/common/%.o: $(COMMON_DIR)/src/%.asm Makefile $(COMMON_DIR)/runtime.mk
	@mkdir -p $(@D)
	$(ASM) $(ASM_FLAGS) -MD $(@:.o=.d) $< -o $@
