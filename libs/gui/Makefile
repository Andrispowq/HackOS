CROSS_COMPILE ?= x86_64-elf-
CXX = $(CROSS_COMPILE)g++
AR = $(CROSS_COMPILE)ar
CFLAGS = -std=gnu++17 -O2 -g -ffreestanding -mno-red-zone -mcmodel=large -fno-pie -fno-stack-protector -fno-omit-frame-pointer -fno-exceptions -fno-rtti -MMD -MP -I ../../include -I ../../common/include
OBJS = bin-int/gui.o bin-int/window.o bin-int/document.o
.DEFAULT_GOAL := all
.PHONY: all clean
all: bin/libgui.a
bin-int/%.o: src/%.cpp Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin/libgui.a: $(OBJS) Makefile
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $(OBJS)
clean:
	rm -rf bin bin-int
-include $(OBJS:.o=.d)
