#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "memory.h"

#include <stdarg.h>

void kprintf(const char* format, ...) 
{
    DISPLAY* curr_display = get_current_display();

    const char* traverse;
    char buffer[30];
    char* string;
    uint64_t index;

    va_list arg;
    va_start(arg, format);

    for(traverse = format; *traverse != 0; traverse++)
    {
        while(*traverse != '%')
        {
            if(*traverse == 0)
                goto end;
            curr_display->putc(*traverse);
            traverse++;
        }

        traverse++;

        switch (*traverse)
        {
        case 'c':
            index = va_arg(arg, int64_t);
            curr_display->putc(index);
            break;

        case 's':
            string = va_arg(arg, char*);
            curr_display->puts(string);
            break;

        case 'd':
            index = va_arg(arg, int64_t);
            if(index < 0)
            {
                index =  -index;
                curr_display->putc('-');
            }

            memset(buffer, 0, 30);
            itoa(buffer, 'd', index);

            curr_display->puts(buffer);
            break;

        case 'x':
            index = va_arg(arg, int64_t);

            memset(buffer, 0, 30);
            itoa(buffer, 'x', index);

            curr_display->puts(buffer);
            break;
        
        default:
            break;
        }
    }

end:
    va_end(arg);
}