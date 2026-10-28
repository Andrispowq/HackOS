# Graphics, console, and windows

[System index](index.md) · [Devices and input](devices.md)

The active kernel display path uses a linear framebuffer supplied by the
bootloader: VBE under BIOS and GOP under UEFI. Early BIOS messages use firmware
text output. Kernel text is rendered into pixels using a PSF1 font.

## Framebuffer and colours

[Framebuffer](../../kernel/src/drivers/screen/framebuffer.h) stores a pixel
address, width, height, bits per pixel, and pitch. Its implementation provides
pixel access, rectangles, clears, and mouse-cursor drawing/restoration.

The `COLOUR(r, g, b, a)` macro in
[screen.h](../../kernel/src/drivers/screen/screen.h) packs a 32-bit integer as
`0xAARRGGBB`. That representation does not by itself negotiate firmware pixel
formats or provide alpha blending.

The framebuffer API measures pitch in 32-bit pixels. The BIOS adapter converts
VBE's byte stride; GOP already supplies pixels per scanline. Pixel access,
rectangles, glyphs, scrolling and backbuffer copies use that stride. Rectangle
width and height are clipped independently. Other pixel formats need explicit
conversion; the UEFI loader currently accepts BGR output only.

## Display and backbuffer

[InitialiseDisplay](../../kernel/src/drivers/screen/screen.cpp) maps the hardware
framebuffer with cache-control flags and allocates a software backbuffer at
virtual address `0xD0000000`. Two additional page-backed RAM buffers follow it:
composition and the previously presented scene. They cost two framebuffer-sized
allocations (about 15.8 MiB extra at 1920x1080 with an unpadded pitch).

When scene content changes, `Display::DrawBackbuffer` copies the console into
composition RAM and draws visible windows and the restore bar there. It compares
the damaged bounds with the previous scene in 32-pixel horizontal blocks,
coalesces adjacent changed blocks, and copies only changed spans to video memory and the saved
scene. The first presentation is unconditional. Comparison never reads device
memory, and all copies respect pitch and screen edges. Text cells, window uploads
and old/new window geometry contribute clipped damage bounds, merged until presentation. Clears, scrolling, focus/mode changes and
window lifecycle changes invalidate the full screen. Scene changes still
recompose the whole screen in RAM, but comparisons and device writes are bounded
by damage. Widely separated changes can expand the union to a large rectangle;
this is not a clipped scene renderer or GPU acceleration.

Cursor-only input takes a separate path: restore the previous 16x16 footprint
from composition RAM and draw the cursor at a single captured position. It does
not recompose or scan the scene. Window geometry, focus, modes, uploads, removal
and console updates still invalidate scene content. Frames remain coalesced by
the existing 16 ms minimum tick interval; this is not a guaranteed 60 FPS rate.
The compositor remains under the single-CPU preemption guard so window buffers
cannot be resized or freed during composition. Framebuffer cache policy is
unchanged; write-combining and SMP require separate work.

The `Display` singleton owns console state: foreground/background colour,
cursor position, character size, and rows/columns. `puts` and `putc` render
PSF1 glyphs and handle newline, carriage return, tab, backspace, and scrolling.
The console is configured around 8-by-16 character cells and the bundled
[zap-light16.psf](../../util/fonts/zap-light16.psf).

Colour fills use `Framebuffer::Clear`/`DrawRect`, keeping complete RGB values.
`memset` is reserved for byte fills. Window buffer pitch is also expressed in
pixels. User uploads are tightly packed pixel arrays, not framebuffer objects.

## Windows

[Window](../../kernel/src/drivers/screen/window.cpp) provides per-window state
and drawing. [WindowManager](../../kernel/src/drivers/screen/window_manager.cpp)
owns a list of windows, applies focus, dragging, resizing and mouse closure, draws into composition RAM,
and resolves window IDs. Window pixels do not alter the console backbuffer.

Userspace uses process-owned opaque handles through `window_create`,
`window_blit`, `window_poll`, `window_close`, `window_state` and `window_event`, plus `window_frame`/`window_upload` for atomic frames. Creation takes a copied title
and bounded geometry; drawing copies validated pixel rectangles. No kernel
framebuffer pointer or object layout is exposed. Exit, fault and termination
release the owner's windows. See [userspace](userspace.md#owned-windows) for
record layouts, quotas and return values.

Idle dispatches ordered keyboard/mouse input, updates the manager, and flushes
dirty rendering under a preemption guard. IRQs do not draw or allocate. Mouse
cursor rendering follows window composition, so it stays above window pixels.
Creation, uploads, mouse closure and resource cleanup mark the display dirty.
Console output also marks it dirty; compositing keeps windows visible over
updated text. Removing a window restores the console from its untouched
backbuffer. The standalone ring-3 shell launches `./USR/BIN/WINDOW` on request;
the demo accepts X/Alt-F4 requests or Q; Ctrl-Alt-Esc forces termination.

## Observing graphics

Use a QEMU window or VNC to inspect output. `-nographic` redirects character
devices but does not turn this framebuffer into a serial console. Automated
tests capture console characters through QEMU debug port `0xe9` and also save
a framebuffer screenshot. To capture text manually, add
`-debugcon file:console.log -global isa-debugcon.iobase=0xe9` to QEMU. This mirror
starts with the kernel console. Step 18 also provides independent COM1 logs
from both C loaders and kernel entry; see [serial diagnostics](streams.md#serial-diagnostics).

See [building and running](../development/building.md) and the
[testing plan](../development/testing.md).

The focused window has a blue title bar and receives translated key events.
Clicking a window raises it; clicking the desktop restores terminal input.
Title dragging and the bottom-right resize grip capture the pointer until
release and clamp geometry to screen/ABI limits. Client presses capture mouse
motion and release, including outside the client. Fixed per-window event queues
and an explicit overflow/state-query contract let user programs recover input
loss; details are in [userspace](userspace.md#window-interaction-and-events).

Optional `tests/qemu_windows.py --firmware bios --profile` measurements time
scene and cursor frames with small/maximized/no windows. They include debugger
round trips and host scheduling, so compare on the same emulator/host rather
than treating them as hardware frame times. `display.framesPresented`,
`sceneFrames` and `pixelsPresented` are debugger-visible cumulative counters;
`pixelsPresented` counts scene spans only, excluding cursor restoration/drawing.

The shell caret is composed above console text and below windows, with its own
small damage bounds and a 500 ms blink period. It leaves console pixels and
serial output untouched; caret control is restricted to the individual terminal
owner.

`window` is an input/painting diagnostic, not a colour picker. Its gradient is a
test pattern, clicking/dragging places a white marker, and ordinary keys or the
mouse wheel recolour the small swatch. Mode/close keys keep their existing
functions. Add `--trace` for event logs used by the regression suite; the normal
demo remains quiet apart from its instructions and exit message.

## Window rendering foundation

Normal windows can grow to the available screen area (at most 4096×2160 client
pixels). The legacy immediate-blit API is retained. New clients stage bounded
pixel uploads behind a generation token, then present the complete replacement
atomically to the compositor. Resize preserves the last committed image while
invalidating stale transactions; allocation failure preserves the old geometry
and buffers. Uncovered resized areas are black until the next presentation.

A 64 MiB aggregate budget includes front, staging and temporary replacement
surfaces, rounded to pages. Surfaces use recoverable contiguous physical-page
allocation below 3 GiB, separate from the kernel heap. Fragmentation can reject
an allocation even with sufficient total free memory. Window objects and their
list use fixed storage; closing or reclaiming windows returns all surface pages.
This is a bounded software renderer, with no shared user mappings or GPU path.

The demo uses tiled frame transactions and sleeps in the existing timed poll
API between events. Window readiness can be polled alongside stream readiness;
closure and queue overflow also wake waiters. See the
[frame and wait ABI](userspace.md#atomic-frame-submission-and-waits).

## GUI interaction

Opt-in GUI windows receive physical down/up, modifier/repeat and separate
US-layout text events, with an authoritative held-key/button snapshot and reset
epoch. Focus changes and ingress loss cancel capture and reset delivered input;
close requests survive queue overflow. The manager supports Alt-Tab,
Shift-Alt-Tab, Alt-Esc, Alt-F4, Alt-F9/F10 and explicit Ctrl-Alt-Esc termination.
GUI Ctrl-C is an application shortcut. Existing decoded terminal and legacy
window interfaces remain available.

The 4 KiB session clipboard requires the focused window owner and is zeroed on
logout. `window --interaction` demonstrates keyboard field/dialog navigation,
clipboard editing, saved text and cancellable close requests using the existing
bounded frame renderer. See [the GUI input ABI](userspace.md#gui-input-and-desktop-interaction).

The userspace [GUI library](gui.md) renders themed widgets and dialogs through
the same atomic frame/input ABI. Layout, selection and widget focus remain in
the client process; the kernel continues to manage windows and composition.
