#pragma once
#include "hackos/types.h"
#include <stdarg.h>

// Integer, character, string and pointer formatting; l/ll/z length modifiers.
// No width, precision or floating-point formats. Unsupported formats return -1.
int vsnprintf(char* buffer, size_t capacity, const char* format, va_list arguments);
int snprintf(char* buffer, size_t capacity, const char* format, ...);
int printf(const char* format, ...);
void printf_backspace();
