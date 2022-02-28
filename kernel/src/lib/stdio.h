#ifndef STDIO_H
#define STDIO_H

#include "drivers/screen/screen.h"

#include <stdarg.h>

void kprintf_vargs(const char* format, va_list args);

void kprintf(const char* format, ...);
void kprintf_backspace();

#endif