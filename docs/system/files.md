# Graphical file manager

[Desktop](desktop.md) · [Filesystem permissions](filesystems.md) · [GUI library](gui.md)

Launch **Files** from the desktop or run `files [folder]`. Each instance owns its
working directory and window. The address field accepts absolute or relative
paths; choose **Go**, **Up**, or **Refresh** to navigate. Select a row and choose
**Open** to enter a folder or preview a file. Tab/Shift-Tab traverse controls;
arrow/Page Up/Page Down keys and the wheel navigate lists. Enter activates focused
buttons. The status line shows the selected entry's size, UID/GID and mode.

Directories use pages of 128 entries with **Previous**/**Next** controls. Entries
remain in filesystem order, including hidden entries. Refresh returns to page one.
Directory enumeration is not a snapshot; refresh after concurrent changes.
Paths and input fields are limited to 255 bytes, following the existing ABI.

File previews are read-only, in the right pane. ASCII text wraps at 48 columns;
tabs become spaces and carriage returns are omitted. Other bytes select hex
preview. Reading is bounded to 32 KiB and display to 256 rows; the status explicitly
marks truncated content. Empty files are supported. Read descriptors close before
returning to the event loop. Preview does not execute programs or modify content;
the graphical editor is a separate milestone.

Enter a name or destination path in the bottom field and choose **New folder**
or **Rename / Move file**. Relative destinations resolve against the displayed
folder. Move never overwrites an existing destination. **Delete file** shows the
captured full path and requires confirmation; Escape cancels. Folder rename and
folder deletion are disabled because the current filesystem supports these
operations only for regular files. There is no recursive deletion or trash.

All access uses the caller's inherited credentials and checked kernel syscalls.
Permission denial is shown in an error dialog. Other errors explain invalid or
existing paths, protected/read-only/open files and storage failures. `/SYSTEM`
protection, exclusive handles and journal recovery remain enforced by the kernel.
Syscalls 52–54 now distinguish `HACKOS_DENIED` for failed parent permission checks;
other failures retain `HACKOS_ERROR`. Existing callers already test nonzero failure.

Storage is static and bounded, with no user heap or kernel ABI additions. The app
uses atomic GUI frame uploads, retries failed paints after 16 ms and blocks in
window poll when idle. Closing, including while a dialog is open, accepts the
close request and releases the window; process exit reclaims remaining resources.

Validation: `python3 tests/qemu_files_gui.py --firmware bios` (also `uefi`) uses a
fresh disposable built image and real keyboard/mouse input. Build first with
`tests/build_images.py --firmware ...`.
