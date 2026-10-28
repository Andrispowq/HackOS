CROSS_COMPILE ?= x86_64-elf-
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
CFLAGS = -std=gnu++17 -O2 -g -ffreestanding -mno-red-zone -mcmodel=large -fno-pie -fno-stack-protector -fno-omit-frame-pointer -fno-exceptions -fno-rtti -MMD -MP -I ../../include -I ../../common/include
.DEFAULT_GOAL := all
.PHONY: all clean
all: bin/window
bin-int/%.o: src/%.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin/window: bin-int/main.o bin-int/widgets.o ../ring3/userspace.ld ../gui/bin/libgui.a ../libc/bin/libc.a Makefile
	@mkdir -p $(@D)
	$(LD) -T ../ring3/userspace.ld -static -nostdlib -o $@ bin-int/main.o bin-int/widgets.o ../gui/bin/libgui.a ../libc/bin/libc.a
../gui/bin/libgui.a: force-gui
	$(MAKE) -C ../gui
.PHONY: force-gui
force-gui:
../libc/bin/libc.a:
	$(MAKE) -C ../libc
clean:
	rm -rf bin bin-int
-include bin-int/*.d
