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

.PHONY: test-login-performance
test-login-performance:
	$(PYTHON) qemu_login_performance.py --firmware $(FIRMWARE)

.PHONY: test-shell-editing test-listing-performance
test-shell-editing:
	$(PYTHON) qemu_shell_editing.py --firmware $(FIRMWARE)
test-listing-performance:
	$(PYTHON) qemu_listing_performance.py --firmware $(FIRMWARE)

.PHONY: test-boot-loader test-loader
test-boot-loader:
	$(PYTHON) boot_loader.py
test-loader:
	$(PYTHON) qemu_loader.py --firmware $(FIRMWARE)

.PHONY: test-surface-allocator
test-surface-allocator:
	python3 surface_allocator.py

.PHONY: test-gui-input
test-gui-input:
	python3 qemu_gui_input.py --firmware $(FIRMWARE)

.PHONY: test-gui-keyboard
test-gui-keyboard:
	python3 gui_keyboard.py

.PHONY: test-gui-library test-gui-widgets
test-gui-library:
	$(PYTHON) gui_library.py
test-gui-widgets:
	$(PYTHON) qemu_gui_library.py --firmware $(FIRMWARE)
