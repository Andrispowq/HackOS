#pragma once
#include "hackos/types.h"
#include <stdarg.h>

// Integer, character, string and pointer formatting; l/ll/z length modifiers.
// Supports -/0 flags, numeric field widths and string/integer precision (0..1024).
// No dynamic */floating-point formats. Unsupported or oversized formats return -1.
int vsnprintf(char* buffer, size_t capacity, const char* format, va_list arguments);
int snprintf(char* buffer, size_t capacity, const char* format, ...);
int printf(const char* format, ...);
void printf_backspace();
