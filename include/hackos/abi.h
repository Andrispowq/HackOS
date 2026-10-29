#pragma once
#include "hackos/types.h"

// Fixed-width, pointer-free syscall records. All reserved bytes are zeroed.
struct HackosFileInfo
{
    uint64_t size;
    uint32_t attributes;
    uint32_t reserved;
    char name[256];
    uint32_t uid, gid, mode, metadataVersion;
    // FAT local/RTC wall time: date in high 16 bits, time in low 16 bits. Zero = unknown.
    uint32_t created, modified;
};

struct HackosProcessInfo
{
    uint64_t pid;
    uint64_t state;
    char name[256];
    uint32_t uid, gid;
    uint64_t parentPid;
    uint64_t processGroup;
};

constexpr uint32_t HACKOS_ROOT_UID = 0;
constexpr uint32_t HACKOS_MAX_GROUPS = 8;
constexpr uint32_t HACKOS_ACCOUNT_NAME = 32;
constexpr uint64_t HACKOS_SPAWN_FOREGROUND = 1;

struct HackosCredentials
{
    uint32_t uid, gid;
    uint32_t groupCount, reserved;
    uint32_t groups[HACKOS_MAX_GROUPS]; // supplementary groups; excludes primary gid
};

struct HackosUserInfo
{
    HackosCredentials credentials;
    char name[HACKOS_ACCOUNT_NAME];
};

struct HackosGroupInfo
{
    uint32_t gid, reserved;
    char name[HACKOS_ACCOUNT_NAME];
};

static_assert(sizeof(HackosCredentials) == 48, "credentials ABI");
static_assert(sizeof(HackosUserInfo) == 80, "user ABI");
static_assert(sizeof(HackosGroupInfo) == 40, "group ABI");

struct HackosClock
{
    uint64_t ticks;
    uint8_t second, minute, hour, day, month, reserved;
    uint16_t year; // Full Gregorian year; all fields are binary, month is 1..12.
};

static_assert(sizeof(HackosFileInfo) == 296, "file ABI");
static_assert(sizeof(HackosProcessInfo) == 296, "process ABI");
static_assert(sizeof(HackosClock) == 16, "clock ABI");
constexpr uint64_t HACKOS_ERROR = ~0ULL;
constexpr uint64_t HACKOS_DENIED = ~1ULL;
constexpr uint64_t HACKOS_RETRY = ~2ULL;
constexpr uint64_t HACKOS_NO_ENTROPY = ~3ULL;
constexpr uint64_t HACKOS_NOT_FOUND = ~4ULL;
constexpr uint64_t HACKOS_FILE_LIMIT = 32768;
constexpr uint64_t HACKOS_SAVE_CREATE = 1;
constexpr uint64_t HACKOS_OPEN_READ = 1;
constexpr uint64_t HACKOS_OPEN_WRITE = 2;
constexpr uint64_t HACKOS_OPEN_CREATE = 4;
constexpr uint32_t HACKOS_NO_UID = 0xffffffff;
constexpr uint64_t HACKOS_AUTH_BROKEN = 0;
constexpr uint64_t HACKOS_AUTH_SETUP = 1;
constexpr uint64_t HACKOS_AUTH_LOGIN = 2;
constexpr uint64_t HACKOS_PASSWORD_MAX = 128;
constexpr uint64_t HACKOS_INPUT_OVERFLOW = 256;
constexpr uint32_t HACKOS_DIRECTORY = 16;
// console: 0 backspace, 1 clear. power: 0 shutdown, 1 restart.

constexpr uint64_t HACKOS_MAX_ARGS = 16;
constexpr uint64_t HACKOS_ARG_BYTES = 1024; // includes all terminating NULs
constexpr uint64_t HACKOS_WAIT_NOHANG = 1;
constexpr uint64_t HACKOS_INPUT_NONBLOCK = 1;
constexpr uint64_t HACKOS_JOB_FOREGROUND = 0;
constexpr uint64_t HACKOS_JOB_BACKGROUND = 1;
constexpr uint64_t HACKOS_JOB_TERMINATE = 2;
constexpr uint64_t HACKOS_JOB_QUERY = 3;
constexpr uint64_t HACKOS_JOB_STOPPED = ~5ULL;
constexpr uint64_t HACKOS_JOB_INTERRUPTED = ~6ULL;
constexpr uint64_t HACKOS_HEAP_BASE = 0x0000400000100000ULL;
constexpr uint64_t HACKOS_HEAP_LIMIT = 512 * 1024;

constexpr uint64_t HACKOS_KEY_SAVE = 19;
constexpr uint64_t HACKOS_KEY_EXIT = 24;
constexpr uint64_t HACKOS_KEY_LEFT = 130;
constexpr uint64_t HACKOS_KEY_RIGHT = 131;
constexpr uint64_t HACKOS_KEY_DELETE = 132;
constexpr uint64_t HACKOS_KEY_UP = 128;
constexpr uint64_t HACKOS_KEY_DOWN = 129;
constexpr uint64_t HACKOS_KEY_INTERRUPT = 3;
constexpr uint64_t HACKOS_KEY_STOP = 26;

// Window dimensions describe the client area; the kernel adds a 30-pixel title bar.
struct HackosWindowSpec
{
    uint32_t x, y, width, height;
    char title[32];
};

constexpr uint64_t HACKOS_WINDOW_STALE = ~10ULL;
constexpr uint64_t HACKOS_WINDOW_BEGIN = 0, HACKOS_WINDOW_PRESENT = 1, HACKOS_WINDOW_CANCEL = 2;
constexpr uint64_t HACKOS_WINDOW_BEGIN_RETAIN = 3;

struct HackosWindowFrame
{
    uint64_t generation;
    uint32_t width, height;
};

struct HackosWindowUpload
{
    uint64_t generation;
    uint32_t x, y, width, height;
};

static_assert(sizeof(HackosWindowFrame) == 16, "window frame ABI");
static_assert(sizeof(HackosWindowUpload) == 24, "window upload ABI");

struct HackosWindowRect
{
    uint32_t x, y, width, height;
};

static_assert(sizeof(HackosWindowSpec) == 48, "window specification ABI");
static_assert(sizeof(HackosWindowRect) == 16, "window rectangle ABI");

constexpr uint32_t HACKOS_WINDOW_KEY = 1;
constexpr uint32_t HACKOS_WINDOW_MOUSE = 2;
constexpr uint32_t HACKOS_WINDOW_FOCUS = 3;
constexpr uint32_t HACKOS_WINDOW_MOVE = 4;
constexpr uint32_t HACKOS_WINDOW_RESIZE = 5;
constexpr uint32_t HACKOS_WINDOW_OVERFLOW = 6;

struct HackosWindowState
{
    uint32_t x, y, width, height;
    int32_t mouseX, mouseY;
    uint32_t buttons, focused;
};

struct HackosWindowEvent
{
    uint32_t type, key;
    int32_t x, y;
    uint32_t width, height, buttons, focused;
};

static_assert(sizeof(HackosWindowState) == 32, "window state ABI");
static_assert(sizeof(HackosWindowEvent) == 32, "window event ABI");

constexpr uint32_t HACKOS_WINDOW_MODE = 7;
constexpr uint32_t HACKOS_WINDOW_WHEEL = 8;
constexpr uint32_t HACKOS_WINDOW_MINIMIZED = 1;
constexpr uint32_t HACKOS_WINDOW_MAXIMIZED = 2;
constexpr uint64_t HACKOS_WINDOW_QUERY = 0;
constexpr uint64_t HACKOS_WINDOW_MINIMIZE = 1;
constexpr uint64_t HACKOS_WINDOW_MAXIMIZE = 2;
constexpr uint64_t HACKOS_WINDOW_RESTORE = 3;

// GUI input is opt-in; the original decoded-key window ABI remains available.
constexpr uint32_t HACKOS_WINDOW_KEY_DOWN = 9, HACKOS_WINDOW_KEY_UP = 10;
constexpr uint32_t HACKOS_WINDOW_TEXT = 11, HACKOS_WINDOW_CLOSE_REQUEST = 12, HACKOS_WINDOW_INPUT_RESET = 13;
constexpr uint32_t HACKOS_MOD_SHIFT = 1, HACKOS_MOD_CTRL = 2, HACKOS_MOD_ALT = 4, HACKOS_MOD_CAPS = 8;
constexpr uint32_t HACKOS_KEY_REPEAT = 1;
constexpr uint64_t HACKOS_INPUT_QUERY = 0, HACKOS_INPUT_ENABLE = 1;
constexpr uint64_t HACKOS_CLIPBOARD_QUERY = 0, HACKOS_CLIPBOARD_READ = 1, HACKOS_CLIPBOARD_WRITE = 2;
constexpr uint64_t HACKOS_CLIPBOARD_LIMIT = 4096;
constexpr uint64_t HACKOS_BUFFER_SMALL = ~11ULL;

// Physical key IDs: set-1 base code, with bit 7 set for E0-prefixed keys.
constexpr uint32_t HACKOS_PHYSICAL_ESCAPE = 0x01, HACKOS_PHYSICAL_TAB = 0x0f;
constexpr uint32_t HACKOS_PHYSICAL_ENTER = 0x1c, HACKOS_PHYSICAL_BACKSPACE = 0x0e;
constexpr uint32_t HACKOS_PHYSICAL_A = 0x1e, HACKOS_PHYSICAL_C = 0x2e, HACKOS_PHYSICAL_X = 0x2d;
constexpr uint32_t HACKOS_PHYSICAL_V = 0x2f, HACKOS_PHYSICAL_S = 0x1f;

struct HackosWindowEvent2
{
    HackosWindowEvent event;
    uint32_t modifiers, flags;
    uint64_t serial;
};

struct HackosWindowInputState
{
    uint64_t keys[4], serial;
    uint32_t modifiers, buttons, focused, closePending;
};

static_assert(sizeof(HackosWindowEvent2) == 48, "GUI event ABI");
static_assert(sizeof(HackosWindowInputState) == 56, "GUI input state ABI");

// Stream descriptors are local indices; legacy file tokens retain their ABI.
constexpr uint64_t HACKOS_STDIN = 0, HACKOS_STDOUT = 1, HACKOS_STDERR = 2;
constexpr uint64_t HACKOS_FD_LIMIT = 16;
constexpr uint64_t HACKOS_IO_LIMIT = 1024;
constexpr uint64_t HACKOS_PIPE_CAPACITY = 4096;
constexpr uint64_t HACKOS_WAIT_FOREVER = ~0ULL;
constexpr uint64_t HACKOS_TIMED_OUT = ~7ULL;
constexpr uint64_t HACKOS_WOULD_BLOCK = ~8ULL;
constexpr uint64_t HACKOS_BROKEN_PIPE = ~9ULL;
constexpr uint64_t HACKOS_OPEN_TRUNCATE = 8;
constexpr uint64_t HACKOS_OPEN_APPEND = 16;
constexpr uint32_t HACKOS_POLL_READ = 1, HACKOS_POLL_WRITE = 2;
constexpr uint32_t HACKOS_POLL_WINDOW = 32; // fd is an owned window handle; request READ.
constexpr uint32_t HACKOS_POLL_HANGUP = 4, HACKOS_POLL_ERROR = 8, HACKOS_POLL_INVALID = 16;

struct HackosPollFd
{
    uint64_t fd;
    uint32_t events, revents;
};

// A valid non-terminal descriptor returns a zeroed record. No root privilege
// is needed; input is available to the owner and its foreground pipeline peers.
constexpr uint32_t HACKOS_TERMINAL = 1, HACKOS_TERMINAL_FOREGROUND = 2;

struct HackosTerminalInfo
{
    uint32_t columns, rows, flags, reserved;
};

static_assert(sizeof(HackosTerminalInfo) == 16, "terminal info ABI");

constexpr uint64_t HACKOS_TERMINAL_RESET = 0, HACKOS_TERMINAL_FONT_SIZE = 1, HACKOS_TERMINAL_BG_COLOUR = 2,
                   HACKOS_TERMINAL_FONT_COLOUR = 3;

struct HackosTerminalSettings
{
    uint32_t fontSize, bgColour, fontColour; // Pixel height and 0xRRGGBBAA colours.
    uint32_t columns, rows, reserved;
};

static_assert(sizeof(HackosTerminalSettings) == 24, "terminal settings ABI");

// spawn_io creates a held job member. group=0 starts a new owned group;
// job_control starts the completed group. Only these three descriptors transfer.
struct HackosSpawnIo
{
    uint64_t streams[3];
    uint64_t group;
};

static_assert(sizeof(HackosPollFd) == 16, "poll ABI");
static_assert(sizeof(HackosSpawnIo) == 32, "spawn I/O ABI");

// Bounded directory pages; offsets are entry indices, not stable snapshots.
constexpr uint64_t HACKOS_READDIR_BATCH_MAX = 16;
// terminal_cursor(back, visible): caret up to 255 cells behind the output position.
constexpr uint64_t HACKOS_TERMINAL_CURSOR_MAX = 255;

// Desktop and terminal endpoints are kernel-issued, process/session scoped.
constexpr unsigned HACKOS_PROCESS_LIMIT = 64;
constexpr unsigned HACKOS_TTY_LIMIT = 8, HACKOS_TTY_CELLS = 160 * 60;
constexpr uint64_t HACKOS_TTY_CREATE = 0, HACKOS_TTY_STATE = 1, HACKOS_TTY_CELLS_READ = 2, HACKOS_TTY_INPUT = 3,
                   HACKOS_TTY_RESIZE = 4;

struct HackosTerminalState
{
    uint64_t revision, shell;
    uint32_t columns, rows, x, y, caretBack, caret;
};

struct HackosDesktopEntry
{
    uint64_t handle, owner;
    uint32_t width, height, mode, focused;
    char title[32];
};

constexpr uint64_t HACKOS_DESKTOP_SCREEN = 0, HACKOS_DESKTOP_PANEL = 1, HACKOS_DESKTOP_ENTRY = 2,
                   HACKOS_DESKTOP_FOCUS = 3, HACKOS_DESKTOP_MINIMIZE = 4, HACKOS_DESKTOP_CLOSE = 5,
                   HACKOS_DESKTOP_BACKGROUND = 6;
static_assert(sizeof(HackosTerminalState) == 40, "terminal snapshot ABI");
static_assert(sizeof(HackosDesktopEntry) == 64, "desktop entry ABI");

// Persistent account changes; running processes retain their credential snapshot.
constexpr uint64_t HACKOS_ACCOUNT_RENAME_USER = 0;
constexpr uint64_t HACKOS_ACCOUNT_RENAME_GROUP = 1;
constexpr uint64_t HACKOS_ACCOUNT_PRIMARY_GROUP = 2;
constexpr uint64_t HACKOS_ACCOUNT_REMOVE_GROUP = 3;
