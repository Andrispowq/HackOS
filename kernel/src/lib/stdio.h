#ifndef STDIO_H
#define STDIO_H

#include "drivers/screen/screen.h"

void kprintf(const char* format, ...);
void kprintf_backspace();

#endif