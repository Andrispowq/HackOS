# Desktop and independent terminals

[System index](index.md) · [GUI library](gui.md) · [Streams](streams.md) · [Accounts](accounts.md)

Normal login starts the authenticated `DESKTOP` program. It displays a persistent
launcher/taskbar and opens a `TERM` window running its own `SHELL`. The graphical login screen offers **Recovery console** (Escape); its text menu
accepts `console` for a direct recovery shell using the same authentication
and credential checks. A second copy of DESKTOP does not acquire desktop authority.

## Using the desktop

- **New terminal** opens another terminal, up to eight concurrently. Windows have
  distinct numbered titles and staggered positions. Each shell has independent
  working directory, history, input, output grid, foreground owner and jobs.
- The hardcoded **Terminal**, **Files**, **Editor**, **Settings**, and **Widgets** desktop
  icons launch on a single click (or Enter/Space while focused). They remain below application windows and are
  not taskbar entries. The desktop owns this background surface; it cannot be
  dragged, resized, minimized or raised above applications.
- **Editor** opens the [graphical text editor](editor.md).
- **Files** opens the [file manager](files.md).
- **Widgets** launches the GUI library demonstration.
- Taskbar buttons list the session's windows, including minimized windows. `>`
  marks the focused window and `_` marks a minimized window. Click one to focus
  or restore it; **Minimize selected** and **Close selected** act on the last
  taskbar selection. Close uses the application's existing close negotiation.
- Alt-Tab/Shift-Alt-Tab cycle windows. Alt-Esc focuses the launcher. Alt-F9
  minimizes, Alt-F10 maximizes/restores and Alt-F4 requests close. Maximized
  windows leave space for the taskbar. Ctrl-Alt-Esc forcibly terminates the
  focused owner, including a failed desktop.
- `exit` closes one shell/terminal and its jobs. Closing a terminal window also
  terminates that terminal's processes. Other terminals remain usable. `logout`
  or the desktop's confirmed **Log out** ends the entire authenticated session.

If the desktop fails or is forcibly terminated, all its session descendants are
terminated and the login agent resumes. The kernel removes the panel and restores
the console during normal resource reclamation. Log in again or open **Recovery console** and choose `console`
for recovery. Closing every terminal leaves the launcher running.

## Terminal contract

The kernel owns eight bounded terminal endpoints. The shared program loader
enforces a 64-process ceiling (including idle/login and unreaped tasks), leaving
room for eight renderer/shell pairs and their jobs. Each parent still has sixteen
child-result slots. Only the process that created
an endpoint can read its grid or submit decoded input. Creation requires an
owned window and authenticated session membership; each controller can create
one endpoint. The child shell inherits credentials, gets a fresh foreground
owner and process group, and becomes a job controller. Descendants inherit their
terminal ID, including pipeline stages and `runas` children. There is no endpoint
transfer or lookup by another process's ID.

Standard terminal descriptors route through the calling process's terminal ID.
Pipes, file redirection, shared open positions and timeouts retain their existing
contracts. Only the foreground owner or its explicitly authorized pipeline peers
can consume input. Ctrl-C and Ctrl-Z target that terminal's foreground group at
a safe scheduler point. Controller death terminates all processes attached to
its endpoint, including detached children, before the endpoint is reused.

Each endpoint has a 256-byte input queue and at most 160×60 cells. Overflow is
reported and readers discard incomplete input according to their existing line
editing rules. Display output supports printable bytes, newline, carriage return,
backspace and tabs. Resize preserves rows, pads wider rows, clips narrower rows
and clamps the cursor. Each endpoint retains the last 512 scrolled-off rows in a
bounded ring, captured as output scrolls even when the renderer is busy. Stored
rows preserve their original columns, padded to 160 cells; resizing does not
reflow history. `clear` wipes both the visible grid and history. Endpoint teardown
wipes the ring before reuse. ANSI escape parsing remains a follow-up.
`terminal get/set/reset` configures each window independently: even font heights
8–64 (width is half the height) and RGBA background/text colours. The default is
8×16 PSF text with background `101b25ff` and text `e5eef5ff`. Settings require the
individual foreground owner, clear screen/history, and last until reset or window
close. Recovery-console settings remain independent. Window chrome and the status
row keep their native font. Glyphs use nearest-neighbour scaling; font-family
selection is not supported.

Wheel scrolls three rows; Shift+PageUp/PageDown scrolls a page; Shift+Home/End
selects the oldest/live view. Ordinary typing and Escape return to live output.
Drag over cells to select text, then Ctrl+Shift+C copies through the existing
focused-owner session clipboard. Ctrl+C remains foreground job interruption.
Selection freezes the displayed snapshot while output continues in the kernel;
scrolling, typing, clear or resize dismisses it. Input reset cancels dragging and
reconciles held buttons. Copy preserves leading spaces, removes trailing spaces
from each selected visual row and separates rows with LF. A selection exceeding
4096 bytes leaves the clipboard unchanged and reports the error in the status row.
Ctrl+Shift+V pastes the focused owner's HackOS session clipboard (up to 4096
bytes). It does not access the host OS clipboard. Printable ASCII is accepted;
tabs and CR/LF line breaks become spaces (CRLF becomes one space), so paste does
not invoke completion or execute commands. Other control/non-ASCII bytes reject
the entire paste. Press Enter to submit; the receiving application's line-length
limit still applies. A separate bounded paste buffer drains before later physical
keystrokes and is cleared on foreground-job handoff, input overflow and endpoint
teardown. Busy input or an earlier pending paste leaves the new paste unapplied
and reports a retry message. There is no drag-selection autoscroll yet.

`TERM` reserves one 16-pixel status row (the initial grid is 90×26) and renders
atomic copied history/grid snapshots using bounded window uploads. It retries
when a resize invalidates a frame. It polls with a 32 ms timeout for output changes;
`DESKTOP` polls at 100 ms for task-list changes. Neither continuously yields in
an idle loop. The terminal caret is steady, rather than blinking.

## ABI and authority

| Syscall | Interface | Contract |
| --- | --- | --- |
| 91 | `auth_desktop(name, password)` | Boot login agent only; same authentication as console login, starts DESKTOP as session leader. |
| 92 | `desktop(operation, value, entry)` | Kernel-designated desktop only; query display, register its fixed panel, enumerate same-session windows, focus/minimize/request close, register an owned background surface. |
| 93 | `terminal_session(operation, value, buffer)` | Create an endpoint with an owned window, copy grid/history/settings, inject decoded input/paste or resize the controller's grid. |
| 94 | `session_logout()` | Session shell or desktop only; terminate the session leader and all descendants. |

`HackosDesktopEntry` is 64 bytes; `focused` reports focus (0/1). Enumeration
returns 1 for a copied entry and 0 at end; indices are not stable across window
changes. Controls use unique window handles and revalidate ownership/session
membership. Screen query also returns 1. Panel registration requires the exact
bottom 100-pixel client geometry and permits only one panel. Clients cannot
request arbitrary always-on-top windows through this interface.

`HackosTerminalState` is 40 bytes. State copy returns 0; the cell copy writes
9600 bytes and returns its revision. Resize packs columns in the low 16 bits
and rows in the high bits, accepting 1–160 columns and 1–60 rows. Input accepts
one decoded byte; Ctrl-C/Ctrl-Z are job-control requests. All destination ranges
are validated by the normal copied-user-memory contract.

Operation 5 (`HACKOS_TTY_VIEW`) copies a 9672-byte `HackosTerminalView` atomically:
the existing 40-byte state, four 64-bit fields (`firstLine`, `liveLine`,
`oldestLine`, `epoch`), then 9600 cells. `value` requests an absolute top-row ID;
`HACKOS_TTY_LIVE` selects the live grid. Requests clamp to oldest/live. IDs restart
on clear; epoch changes on clear/resize so controllers can discard stale views.
Unused cells are spaces. The complete destination must be writable; success
returns zero. Access requires the endpoint's individual controller, including
for UID 0. Existing state/cells operations retain their original layout/semantics.

Operation 6 (`HACKOS_TTY_PASTE`) accepts a copied buffer of `value` bytes (0–4096),
normalizes it as above, and queues it atomically. It returns zero, `HACKOS_ERROR`
for bad bytes/ranges, or `HACKOS_WOULD_BLOCK` while input/paste is pending or no
foreground owner exists. Operation 7 (`HACKOS_TTY_SETTINGS`) copies the existing
24-byte settings record for the controller's endpoint. Both operations require
the individual endpoint controller. Foreground applications use syscalls 81/82;
set operation 4 (`HACKOS_TERMINAL_DEFAULT`) restores just the key specified by
value 1–3 using the applicable console/window defaults.

The session ID belongs to the authenticated leader, not each terminal shell.
Individual terminal exit never ends sibling terminals. Whole-session teardown
clears the clipboard and reclaims windows, endpoint state, streams and private
pages. Desktop authority is not inherited by spawned children, even UID 0 ones.

Desktop operation 6 (`HACKOS_DESKTOP_BACKGROUND`) designates one owned window as
an immutable bottom layer. It requires desktop authority and exact geometry:
x/y 1, client width screen-width minus 2 and height screen-height minus 164.
Its client retains the normal 30-pixel coordinate offset, but has no chrome.
It is excluded from Alt-Tab and taskbar enumeration. Client input still uses the
owned window queue. Desktop exit reclaims both panel and background; the pair
uses two of the sixteen window slots and the normal aggregate surface budget.

The launcher also opens [Settings](settings.md). The right side displays the RTC
date and time as YYYY-MM-DD HH:MM:SS UTC, updated once per second. The guest
assumes the RTC uses UTC; there is no time-zone configuration yet.
