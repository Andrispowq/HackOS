#include "stdio.h"
#include "stdint.h"
#include "string.h"

void printf(const char* format, ...) 
{
    DISPLAY* curr_display = get_current_display();

    uint8_t** arg = (uint8_t**) &format;
    uint8_t c;
    uint8_t buf[20];

    arg++;

    while((c = *format++) != 0) 
    {
        if (c != '%')
        {
            curr_display->putc(c);
        }
        else 
        {
            uint8_t *p, *p2;
            int pad0 = 0, pad = 0;

            c = *format++;
            if (c == '0') 
            {
                pad0 = 1;
                c = *format++;
            }

            if (c >= '0' && c <= '9') 
            {
                pad = c - '0';
                c = *format++;
            }

            switch (c) 
            {
            case 'X':
            case 'x':
            case 'd':
            case 'u':
                itoa(buf, c, *((int *) arg++));
                p = buf;
                goto string;
            case 's':
                p = *arg++;
                if (!p)
                    p = (uint8_t*)"(null)";
            string:
                for (p2 = p; *p2; p2++);
                for (; p2 < p + pad; p2++)
                    curr_display->putc((char)(pad0 ? '0' : ' '));
                while (*p)
                    curr_display->putc((char)(*p++));
                break;
            default:
                curr_display->putc((char)(*((int*) arg++)));
                break;
            }
        }
    }
}