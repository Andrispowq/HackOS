#include "stdio.h"

#include "kernel_interface/syscall.h"
#include <stdarg.h>

void printf(const char* format, ...) 
{
    va_list arg;
    va_start(arg, format);

    syscall_kprintf(format, arg);
    
    va_end(arg);
}

void kprintf_backspace()
{
    syscall_kprintf_backspace();
}