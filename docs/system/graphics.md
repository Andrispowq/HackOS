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

Drawing and copying currently assume tightly packed 32-bit pixels and often
use `width` as the row stride rather than the supplied pitch. Supporting other
GOP/VBE modes requires checking pixel masks/order, pitch units, and clipping at
every buffer boundary. Palette names or legacy VGA colour choices should be
converted to this pixel representation at the drawing boundary.

## Display and backbuffer

[InitialiseDisplay](../../kernel/src/drivers/screen/screen.cpp) maps the hardware
framebuffer with cache-control flags and allocates a software backbuffer at
virtual address `0xD0000000`. `Display::DrawBackbuffer` copies the entire software
buffer into the hardware buffer.

The `Display` singleton owns console state: foreground/background colour,
cursor position, character size, and rows/columns. `puts` and `putc` render
PSF1 glyphs and handle newline, carriage return, tab, backspace, and scrolling.
The console is configured around 8-by-16 character cells and the bundled
[zap-light16.psf](../../util/fonts/zap-light16.psf).

Colour fills currently rely on the project's word-oriented `memset` behavior.
Replacing it with a standard byte-fill implementation without changing these
callers would alter the displayed colours. A separate pixel-fill operation
would make that distinction explicit.

## Windows

[Window](../../kernel/src/drivers/screen/window.cpp) provides per-window state
and drawing. [WindowManager](../../kernel/src/drivers/screen/window_manager.cpp)
owns a list of windows, updates them, draws them into the display backbuffer,
and resolves window IDs.

The syscall layer exposes creation, destruction, title changes, and access to
a window buffer. Buffer access currently returns kernel-owned state directly;
it is an experimental shared-memory interface rather than a protected graphics
API. The userspace library carries its own framebuffer/display types, so layout
changes must be coordinated across both sides.

The regular update/draw loop lives in `kernel_task`, which is not started while
tasking initialization is disabled. Normal `kernel_main` draws the initial
backbuffer once and then halts between interrupts.

## Observing graphics

Use a QEMU window or VNC to inspect output. `-nographic` redirects character
devices but does not turn this framebuffer into a serial console. Automated
boot reporting should use a separate serial/debug channel; visual tests can
then focus on clipping, colours, glyphs, and buffer composition.

See [building and running](../development/building.md) and the
[testing plan](../development/testing.md).
