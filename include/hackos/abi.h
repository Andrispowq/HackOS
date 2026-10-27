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
    uint8_t second, minute, hour, day, month, year;
    uint8_t reserved[2];
};

static_assert(sizeof(HackosFileInfo) == 288, "file ABI");
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
constexpr uint32_t HACKOS_POLL_HANGUP = 4, HACKOS_POLL_ERROR = 8, HACKOS_POLL_INVALID = 16;

struct HackosPollFd
{
    uint64_t fd;
    uint32_t events, revents;
};

// spawn_io creates a held job member. group=0 starts a new owned group;
// job_control starts the completed group. Only these three descriptors transfer.
struct HackosSpawnIo
{
    uint64_t streams[3];
    uint64_t group;
};

static_assert(sizeof(HackosPollFd) == 16, "poll ABI");
static_assert(sizeof(HackosSpawnIo) == 32, "spawn I/O ABI");
