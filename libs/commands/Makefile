CROSS_COMPILE ?= x86_64-elf-
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
CFLAGS = -std=gnu++17 -O2 -g -ffreestanding -mno-red-zone -mcmodel=large -fno-pie -fno-stack-protector -fno-omit-frame-pointer -fno-exceptions -fno-rtti -MMD -MP -I ../../include -I ../../common/include

PROGRAMS = help clear terminal echo time ping nslookup sysinfo ps kill ls wd cat read grep wc head tail tee more sort uniq cut tr shutdown restart id whoami users groups useradd groupadd usermod runas passwd xxd mkdir rm mv edit chmod chown
.DEFAULT_GOAL := all
.PHONY: all clean
all: $(addprefix bin/,$(PROGRAMS))
bin-int/%.o: src/%.cpp src/command.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin-int/read.o: src/cat.cpp src/command.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin-int/whoami.o: src/id.cpp src/accounts.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -DWHOAMI -c $< -o $@
bin-int/shutdown.o: src/power.cpp src/command.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -c $< -o $@
bin-int/restart.o: src/power.cpp src/command.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) -DRESTART -c $< -o $@
bin/%: bin-int/%.o ../ring3/userspace.ld ../libc/bin/libc.a Makefile
	@mkdir -p $(@D)
	$(LD) -T ../ring3/userspace.ld -static -nostdlib -o $@ $< ../libc/bin/libc.a
../libc/bin/libc.a:
	$(MAKE) -C ../libc
clean:
	rm -rf bin bin-int
.SECONDARY:
-include bin-int/*.d
