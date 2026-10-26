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
