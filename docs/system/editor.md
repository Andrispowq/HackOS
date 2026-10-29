# Graphical text editor

[Desktop](desktop.md) · [Files](files.md) · [GUI library](gui.md)

Launch **Editor** from the panel, choose **Edit text** on a file in Files, or run
`editor [path]`. Documents are ASCII text with LF and tab, bounded to 32 KiB.
Binary, CR/CRLF and oversized files are rejected without replacing the current
buffer. Files remain closed between open/save operations.

Use **New**, **Open...**, **Save**, or **Save as...**, also Ctrl-N/O/S. Open/save
forms have a folder path, Up/Go, 64-entry directory pages, a selectable list and
a file-path field. **Browse selection** enters a selected folder. Relative file
paths resolve against the displayed folder; absolute paths are accepted.
Saving to an existing path through Save as requires **Replace** confirmation.
Normal Save writes the current path through the checked, journaled save syscall.

The text area supports multiline insertion, Backspace/Delete, arrows, Home/End,
Ctrl-Home/End, Page Up/Down, Shift-selection and mouse-drag selection. Ctrl-A
selects all; Ctrl-C/X/V copy/cut/paste using the session clipboard. Clipboard
transfers are limited to 4 KiB; oversized selections/pastes or invalid bytes leave
the document intact. Tab traverses widgets, Ctrl-I inserts a tab (displayed as one
cell). The wheel scrolls vertically; cursor movement reveals its position,
including horizontal scrolling for long lines. Text does not wrap automatically.
The status reports line, column, byte count and unsaved state.

New, Open and Close prompt **Save / Discard / Cancel** when changes exist. Cancel
preserves the document and releases a pending kernel close request. Failed saves
retain the buffer and report permission, path, storage and resource errors.
Opening another document is atomic: its bytes are validated before replacing the
old buffer. Normal window resizing uses atomic GUI frames. Forced process death
or session logout cannot preserve unsaved edits; there is no autosave or undo.

The editor uses static storage, the shared `Gui::TextDocument`/`TextArea`, and
copied filesystem/window interfaces. `libs/editor` links `libgui.a` and `libc.a`;
the packaged program is `/usr/bin/editor`. `tests/qemu_editor.py` validates BIOS
and UEFI with disposable images and copied OVMF VARS.
