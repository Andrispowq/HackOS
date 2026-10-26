# Test entry points are separate from the production Makefiles.
PYTHON ?= python3
FIRMWARE ?= bios
.PHONY: test test-host check-images test-bios-chain test-qemu build-images
test:
	$(PYTHON) run.py all
test-host:
	$(PYTHON) run.py host
check-images:
	$(PYTHON) run.py images
test-bios-chain:
	$(PYTHON) run.py bios-chain
build-images:
	$(PYTHON) build_images.py --firmware $(FIRMWARE)
test-qemu:
	$(PYTHON) qemu_boot.py --firmware $(FIRMWARE) $(if $(IMAGE),--image $(IMAGE)) $(if $(ELF),--elf $(ELF))
