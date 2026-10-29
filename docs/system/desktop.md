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
and clamps the cursor. There is no scrollback, ANSI escape parser, terminal text
selection or configurable font in this version. Window terminals use fixed 8×16
PSF text and a dark palette; `terminal get` reports their actual geometry/settings,
while `terminal set/reset` returns denied. Recovery-console settings remain
configurable and independent of window terminals.

`TERM` renders copied, revision-checked snapshots using bounded atomic window
uploads. It retries if output changes between the state and cell copies or a
resize invalidates a frame. It polls with a 32 ms timeout for output changes;
`DESKTOP` polls at 100 ms for task-list changes. Neither continuously yields in
an idle loop. The terminal caret is steady, rather than blinking.

## ABI and authority

| Syscall | Interface | Contract |
| --- | --- | --- |
| 91 | `auth_desktop(name, password)` | Boot login agent only; same authentication as console login, starts DESKTOP as session leader. |
| 92 | `desktop(operation, value, entry)` | Kernel-designated desktop only; query display, register its fixed panel, enumerate same-session windows, focus/minimize/request close. |
| 93 | `terminal_session(operation, value, buffer)` | Create an endpoint with an owned window, copy state/cells, inject a decoded byte or resize the controller's grid. |
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

The session ID belongs to the authenticated leader, not each terminal shell.
Individual terminal exit never ends sibling terminals. Whole-session teardown
clears the clipboard and reclaims windows, endpoint state, streams and private
pages. Desktop authority is not inherited by spawned children, even UID 0 ones.
