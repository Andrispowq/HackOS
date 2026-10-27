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

.PHONY: test-shell
test-shell:
	$(PYTHON) qemu_shell.py --firmware $(FIRMWARE)

.PHONY: test-user
test-user:
	$(PYTHON) qemu_user.py --firmware $(FIRMWARE)

.PHONY: test-windows
test-windows:
	$(PYTHON) qemu_windows.py --firmware $(FIRMWARE)

.PHONY: test-accounts
test-accounts:
	$(PYTHON) qemu_accounts.py --firmware $(FIRMWARE)

.PHONY: test-files
test-files:
	$(PYTHON) qemu_files.py --firmware $(FIRMWARE)

.PHONY: test-storage
test-storage:
	$(PYTHON) qemu_storage.py --firmware $(FIRMWARE)

.PHONY: test-usability
test-usability:
	$(PYTHON) qemu_usability.py --firmware $(FIRMWARE)

.PHONY: test-streams
test-streams:
	$(PYTHON) qemu_streams.py --firmware $(FIRMWARE)

.PHONY: test-utilities
test-utilities:
	$(PYTHON) qemu_utilities.py --firmware $(FIRMWARE)

.PHONY: test-filters test-filter-commands
test-filters:
	$(PYTHON) qemu_filters.py --firmware $(FIRMWARE)
test-filter-commands:
	$(PYTHON) filter_commands.py

.PHONY: test-text-tools
test-text-tools:
	$(PYTHON) qemu_text_tools.py --firmware $(FIRMWARE)

.PHONY: test-terminal
test-terminal:
	$(PYTHON) qemu_terminal.py --firmware $(FIRMWARE)
