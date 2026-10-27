#pragma once
#include "hackos/types.h"

// Fixed-width, pointer-free syscall records. All reserved bytes are zeroed.
struct HackosFileInfo { uint64_t size; uint32_t attributes; uint32_t reserved; char name[256]; };
struct HackosProcessInfo { uint64_t pid; uint64_t state; char name[256]; };
struct HackosClock { uint64_t ticks; uint8_t second, minute, hour, day, month, year; uint8_t reserved[2]; };
static_assert(sizeof(HackosFileInfo) == 272, "file ABI");
static_assert(sizeof(HackosProcessInfo) == 272, "process ABI");
static_assert(sizeof(HackosClock) == 16, "clock ABI");
constexpr uint64_t HACKOS_ERROR = ~0ULL;
constexpr uint64_t HACKOS_DENIED = ~1ULL;
constexpr uint64_t HACKOS_INPUT_OVERFLOW = 256;
constexpr uint32_t HACKOS_DIRECTORY = 16;
// console: 0 backspace, 1 clear. power: 0 shutdown, 1 restart.

constexpr uint64_t HACKOS_MAX_ARGS = 16;
constexpr uint64_t HACKOS_ARG_BYTES = 1024; // includes all terminating NULs
constexpr uint64_t HACKOS_WAIT_NOHANG = 1;

constexpr uint64_t HACKOS_KEY_UP = 128;
constexpr uint64_t HACKOS_KEY_DOWN = 129;
constexpr uint64_t HACKOS_KEY_INTERRUPT = 3;

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
