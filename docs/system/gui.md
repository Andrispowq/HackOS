# Userspace GUI library

[System index](index.md) · [Window ABI](userspace.md#owned-windows) · [Testing](../development/testing.md)

`libs/gui` builds a freestanding C++17 archive, `libgui.a`. Its public API is
[`hackos/gui.h`](../../include/hackos/gui.h). Link the archive before `libc.a`,
using the existing userspace linker script and freestanding/no-red-zone flags.
`window --widgets` is the packaged consumer; the previous gradient and
`--interaction` diagnostics remain available. The kernel ABI is unchanged.

## Storage and lifecycle

Declare `Gui::View` and `Gui::Window` in zero-initialized static storage. Call
`View::Init()` before adding controls; a view is larger than the 16 KiB user
stack. The library uses no heap, virtual functions or global constructors.
Window adapters must start zero-initialized and must not be copied while owning
a handle. `Open` loads the packaged 8×16 PSF1 font, creates an owned window and
enables rich input. Font/read/input failures close acquired resources. Call
`Close` explicitly; process exit also reclaims the kernel resources. There is
no automatic destructor or global-constructor runtime dependency.

A view contains at most 64 nodes including its root (ID 0). IDs are stable until
`Init`; nodes cannot be removed/reparented. Hide a node instead. Labels, field
text and checkbox/button captions are copied, bounded to 255 bytes plus NUL.
Text fields accept printable ASCII; other captions allow LF. Invalid text is
rejected before replacing existing contents. A list borrows up to 1024 string
pointers; the array and strings must remain valid while installed. List rows
render at most 256 bytes. `Get` returns a read-only pointer to current node state, valid
until view reinitialization. Do not mutate node storage through casts.

## Drawing and layout

`Canvas::Init` binds caller-owned pixels, stride and global client-coordinate
tile bounds. The caller supplies enough storage for the declared stride/height.
Dimensions are bounded to 4096×2160 and stride to 4096 pixels. Invalid geometry
produces an empty canvas. `Fill`, `Border` and PSF `Text` intersect the canvas clip
and tile. `SetClip` replaces the current clip within the tile; callers intersect
with `Clip()` to preserve an outer clip. Rectangle intersection uses widened
arithmetic for offscreen/negative/extreme coordinates. `Measure` reports the
bounded monospace text extent, including explicit line breaks. There is no
Unicode, shaping, automatic word wrapping, alpha blending or arbitrary font
scaling in this version.

The root is a column. `Add(Row/Column, parent)` creates nested layout containers;
only containers accept children. `SetLayout` specifies requested width/height,
primary-axis flex weight, padding and gap. Zero cross-axis size fills the parent;
zero row widths use a widget's preferred width. Fixed primary sizes consume
space first, gaps are reserved, and flex weights divide the remainder. Rounding
assigns all remaining space to the final flexible child. Oversized fixed content
is clipped at the parent; it cannot draw or receive hits outside its ancestor
clips. Hidden nodes consume no space and neither hidden nor disabled subtrees
participate in focus/hit testing. Each resize recomputes the same tree.

`DefaultTheme` supplies consistent background, panel, field, text, muted,
border, accent and selection colours. `SetTheme` replaces the palette and
invalidates drawing. Focus has an accent border/caret; disabled controls use
muted text. Layout sizes and colours are user-space choices.

## Widgets, input and actions

- Labels render bounded text. Buttons activate on Enter/Space (without repeat)
  or a press followed by release inside the same control. Release outside,
  focus loss or overflow cancels pointer activation.
- Checkboxes use the same activation rules and emit `Change` with their new
  value. `SetChecked` changes state programmatically without emitting an action.
- Text fields support insertion, Backspace/Delete, Home/End, Left/Right,
  Shift-selection, pointer dragging, Ctrl-A and Ctrl-C/X/V. Selection replacement
  and clipboard insertion are atomic; denied, oversized or non-ASCII pastes
  preserve contents. Horizontal scrolling keeps the caret visible and adjusts
  when the field expands. Clipboard callbacks are optional in the pure core;
  the window adapter supplies the focused-session clipboard syscalls.
- Lists support pointer selection, Up/Down, Home/End, Page Up/Down and wheel
  scrolling. Keyboard selection reveals its row; wheel scrolling does not
  silently change selection. A position indicator shows the visible range.
- Tab/Shift-Tab traverse enabled controls in creation order, wrapping at the
  ends. Widget focus is separate from kernel window focus.

`Handle` returns one `Action` (`None`, `Activate`, `Change`, `Close` or `Dialog`)
per input event. Programmatic setters invalidate the view without emitting
callbacks. The application handles actions after dispatch, avoiding reentrant
widget mutation during input handling. Input resets preserve text/selection,
clear pending pointer gestures, and redraw focus state; they never infer edits
or clicks from lost input. Kernel desktop shortcuts retain their step-2 behavior.

## Modal dialogs and close negotiation

`OpenDialog` copies a title (63 bytes), multiline message (255 bytes), and one to
three labels (31 bytes each), with an explicit cancel index. Only one dialog may
be open. Tab/Shift-Tab cycle its buttons, Enter/Space choose, Escape selects
cancel, and pointer activation requires a matching press/release. Background
widgets receive no input. Dismissal restores the previous valid widget focus.
Space activation consumes its paired text event so dismissal cannot type into
the restored text field. Dialogs center within the client and clip on very small clients; applications
should choose useful initial sizes for their content.

A kernel close request always becomes a `Close` action, even while a dialog is
open. Applications must accept, reject or defer it explicitly; silently dropping
it would strand the kernel's pending-close latch. The demo rejects a close
request while its About/confirmation dialog is already open. Otherwise it opens
Close/Cancel, calls `ReplyClose` for kernel requests, and uses `Close` for its own
Close button. The library never saves application data or kills a process on its
own. Applications can implement save/discard/cancel using the same dialog API.

## Event loop and painting

`Window::Next` copies one event and returns 1, 0 for no event, or `HACKOS_ERROR`.
On reset/overflow it queries authoritative window/input state and resynchronizes
the view. `Paint` begins an atomic frame, lays out to its dimensions, renders
bounded strips of at most 1024 pixels and presents only when every upload
succeeds. A failed upload/present cancels the transaction, keeps the old committed
image and leaves the view dirty. `Wait` uses window readiness in timed `poll`.

The [demo event loop](../../libs/window/src/widgets.cpp) is a complete consumer
using only the public API. The demo drains at most 16 events before attempting a dirty repaint. It sleeps
16 ms after a render failure and blocks in `Wait` when idle or minimized. It
repaints after restore/resize. Applications should use this bounded loop pattern,
handle every action, and explicitly close the adapter on termination. Rendering
is currently a full client repaint; damage tracking remains a later milestone. The desktop, graphical login and
[file manager](files.md) also use this library.

Text fields can opt into `SetSecret(id, true)`: rendering uses asterisks,
clipboard operations are disabled, and shortened/cleared values erase the
remaining buffer. Call `SetText(id, "")` before discarding a secret field.

## Multiline documents and icons

`TextArea` borrows a static `TextDocument` through `SetDocument`. The document
contains 32768 bytes plus a terminator, selection/cursor offsets and a viewport;
`Load` validates the whole input before replacing it. `Replace` preserves data on
invalid/oversized input. Bind only one widget to a document, keep it alive for the
view's lifetime, and do not pass overlapping document storage to `Replace`.
Clipboard cut/paste is atomic and limited to 4096 bytes. Text uses ASCII/LF/tab;
tab displays as one cell. See [editor](editor.md) for keys and behavior.
`ActionKind::Command` reports Ctrl-N/O/S by physical key in `value`; applications
choose how to handle these commands. Modals continue to trap input.

`IconButton` is a normal focusable button with a built-in code-drawn terminal
(icon 1) or folder (icon 2), selected through `SetIcon`. It follows the usual
clipping, focus and press/release activation rules. Desktop uses these two fixed
icons; no image assets or icon discovery mechanism are required.
