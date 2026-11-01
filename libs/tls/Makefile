CROSS_COMPILE ?= x86_64-elf-
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
ROOTS ?= roots.h
CFLAGS = -std=c99 -Os -g -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -mcmodel=large -mno-red-zone -fno-omit-frame-pointer -ffunction-sections -fdata-sections -MMD -MP -Ifreestanding -Ivendor/bearssl/inc -Ivendor/bearssl/src -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_WIN32_RAND=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0 -DHACKOS_TLS_ROOTS='"$(ROOTS)"'
SOURCES = $(shell find vendor/bearssl/src -name '*.c') src/engine.c src/memory.c
OBJECTS = $(patsubst %.c,bin-int/%.o,$(SOURCES))
.DEFAULT_GOAL := all
.PHONY: all clean
all: bin/libtls.a
bin-int/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
bin/libtls.a: $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(OBJECTS)
clean:
	rm -rf bin bin-int
-include $(OBJECTS:.o=.d)
