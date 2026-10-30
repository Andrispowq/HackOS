# Minimal HTML browser

[System index](index.md) · [Networking](networking.md) · [GUI library](gui.md)

`browser [http://host[:port]/path]` opens the standalone ring-3 Browser window.
It is also available in the desktop launcher. With Compose's HTTP demo running,
the default page is `http://10.0.2.2:8080/browser/`. The linked guide and long page
provide a local navigation/scrolling demonstration without Internet access.

## Controls

Enter an address and press Enter or Go. Back revisits the previous successful
URL; Reload fetches the current successful page again. Home opens the local demo.
Stop cancels a pending DNS/TCP request. A failed or cancelled navigation keeps the
previous document visible and reports the error in the status bar. The address
field retains the requested URL; Reload returns to the displayed page's URL.

Click an underlined link to navigate. With the page focused, Left/Right select
links and Enter follows the selected link. Tab/Shift-Tab move among toolbar
controls and the page. Wheel/Up/Down scroll; Page Up/Page Down/Space scroll a page;
Home/End jump to the top/bottom. Clicking the upper/lower half of the scrollbar
pages up/down. Window resizing reflows text; long words and preformatted lines
wrap at the edge. The scrollbar is an indicator with paging, not a draggable thumb.

## Supported document subset

The browser renders ASCII using the packaged 8x16 font:

- `h1`–`h6`: bold, coloured headings with block spacing (one font size).
- `p`, `div`, `br`, `hr`: paragraphs, blocks, line breaks and a short text rule.
- `b`, `strong`: bold text, including nested bold spans.
- `ul`, `ol`, `li`: flat bullet/numbered lists; nested-list hierarchy is not modelled.
- `pre`: preserved spaces/newlines and four-space tabs, with edge wrapping.
- `a href`: links with quoted or unquoted attributes and relative HTTP references.
- `amp`, `lt`, `gt`, `quot`, `apos`, `nbsp`, decimal/hex numeric entities.

Normal text collapses whitespace. Unknown tags leave their text visible.
Comments and `head`, `title`, `script`, and `style` contents are skipped. This is
a bounded forgiving tokenizer, not an HTML5 parser or DOM. Malformed/unclosed
markup may omit the remaining text. Unicode and unsupported control bytes render
as `?`; there is no UTF-8 font support. The parser cannot execute content.

There is no CSS, JavaScript, image loading, form submission, cookie storage,
authentication, proxy support, download UI, TLS/HTTPS or automatic redirects.
No linked resource is fetched automatically. Plain-text responses pass through
the same HTML tokenizer, so literal markup may be interpreted. HTTP errors and
redirects keep the old page; compressed bodies are rejected by the HTTP decoder.

Relative links resolve against the displayed page, including root-relative,
scheme-relative, query-only and dot-segment paths. Only `http://` is accepted.
Fragments are stripped; following a fragment reloads the page at the top rather
than jumping to an element. `base` tags are ignored. No URL is passed to a shell.

## Bounds and lifecycle

Pages use the existing HTTP decoder's 32 KiB decoded-body limit, strict framing,
header and wire-byte bounds. The browser additionally limits URLs to 255 bytes,
links to 64 and history to 16 previous successful URLs. Excess links retain their
text without navigation. Layout stores at most 32768 glyphs; expansion beyond
that limit is labelled in the status bar. Lists number 1–99, then restart at 1.

The document's glyph/layout arrays are allocated once from the private 512 KiB
userspace heap (about 336 KiB); the HTTP fetch buffer adds about 36 KiB. History
and GUI state live in the executable's bounded data segment. Kernel executable
page limits and syscall interfaces are unchanged. The browser replaces document
contents only after a complete successful HTTP response; Back refetches, rather
than caching documents or restoring scroll position.

DNS and TCP advance without blocking the GUI. A single active request has a
10-second inactivity limit and a 30-second whole-load limit. Each GUI iteration
reads at most eight 512-byte TCP records, then handles input and rendering again.
Timed poll combines window and socket readiness; DNS uses a 20 ms query interval.
Navigation cancels the prior request; Stop/close/exit release the owned socket
and resolver slot. Completed framing closes the single-use socket immediately.

## Validation

`python3 tests/html.py` runs ASan/UBSan checks of the actual parser, layout and URL
resolver, including exact-sized truncated inputs, mutations, unsupported schemes,
entities, whitespace, hidden contents, full-size documents and expansion bounds.
`tests/gui_library.py` covers the custom widget's input and drawing integration.

After building, run `python3 tests/qemu_browser.py --firmware bios --image
out/browser/bios/HackOS.img` and the UEFI equivalent in the test container (GDB is
required). The test serves generated HTML over loopback HTTP and exercises actual
mouse/keyboard navigation, Back/Reload/Home, address entry, scrolling, resize,
errors, Stop, close during a slow response, and restart. It inspects document and
window state and captures screenshots under `tests/out/browser-*`.
