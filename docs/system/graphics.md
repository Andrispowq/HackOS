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
virtual address `0xD0000000`. `Display::DrawBackbuffer` copies the entire software
buffer into the hardware buffer, then composites visible windows over it.

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
owns a list of windows, applies focus, dragging, resizing and mouse closure, draws onto the physical display,
and resolves window IDs. Window pixels do not alter the console backbuffer.

Userspace uses process-owned opaque handles through `window_create`,
`window_blit`, `window_poll`, `window_close`, `window_state` and `window_event`. Creation takes a copied title
and bounded geometry; drawing copies validated pixel rectangles. No kernel
framebuffer pointer or object layout is exposed. Exit, fault, kill and Ctrl-C
release the owner's windows. See [userspace](userspace.md#owned-windows) for
record layouts, quotas and return values.

Idle dispatches ordered keyboard/mouse input, updates the manager, and flushes
dirty rendering under a preemption guard. IRQs do not draw or allocate. Mouse
cursor rendering follows window composition, so it stays above window pixels.
Creation, uploads, mouse closure and resource cleanup mark the display dirty.
Console output also marks it dirty; compositing keeps windows visible over
updated text. Removing a window restores the console from its untouched
backbuffer. The standalone ring-3 shell launches `./USR/BIN/WINDOW` on request;
the demo closes with X, Q, or Ctrl-C.

## Observing graphics

Use a QEMU window or VNC to inspect output. `-nographic` redirects character
devices but does not turn this framebuffer into a serial console. Automated
tests capture console characters through QEMU debug port `0xe9` and also save
a framebuffer screenshot. To capture text manually, add
`-debugcon file:console.log -global isa-debugcon.iobase=0xe9` to QEMU. This mirror
starts with the kernel console; it is not an early-loader serial driver.

See [building and running](../development/building.md) and the
[testing plan](../development/testing.md).

The focused window has a blue title bar and receives translated key events.
Clicking a window raises it; clicking the desktop restores terminal input.
Title dragging and the bottom-right resize grip capture the pointer until
release and clamp geometry to screen/ABI limits. Client presses capture mouse
motion and release, including outside the client. Fixed per-window event queues
and an explicit overflow/state-query contract let user programs recover input
loss; details are in [userspace](userspace.md#window-interaction-and-events).
