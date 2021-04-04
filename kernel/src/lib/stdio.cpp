#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "memory.h"

#include <stdarg.h>

char doubleTo_StringOutput[128];
char intBuffer[120];
const char* string_from_double(double value, uint8_t decimalPlaces)
{
    if (decimalPlaces > 20) decimalPlaces = 20;

    char* intBuff = intBuffer;

    int64_t intVal = value;
    if(intVal < 0)
    {
        intBuff[0] = '-';
        itoa((uint8_t*)(intBuff + 1), 'd', (uint64_t)(-(int64_t)value));
    }
    else
    {
        itoa((uint8_t*)intBuff, 'd', (uint64_t)value);
    }

    char* doublePtr = doubleTo_StringOutput;

    if (value < 0)
    {
        value *= -1;
    }

    while(*intBuff != 0)
    {
        *doublePtr = *intBuff;
        intBuff++;
        doublePtr++;
    }

    *doublePtr = '.';
    doublePtr++;

    double newValue = value - (int)value;

    for (uint8_t i = 0; i < decimalPlaces; i++){
        newValue *= 10;
        *doublePtr = (int)newValue + '0';
        newValue -= (int)newValue;
        doublePtr++;
    }

    *doublePtr = 0;
    return doubleTo_StringOutput;
}

void kprintf(const char* format, ...) 
{
    Display* curr_display = Display::SharedDisplay();

    const char* traverse;
    char buffer[30];
    char* string;
    uint64_t index;
    double d_index;

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
            itoa((uint8_t*)buffer, 'd', index);

            curr_display->puts(buffer);
            break;

        case 'x':
            index = va_arg(arg, int64_t);

            memset(buffer, 0, 30);
            itoa((uint8_t*)buffer, 'x', index);

            curr_display->puts(buffer);
            break;

        case 'f':
            d_index = va_arg(arg, double);

            curr_display->puts(string_from_double(d_index, 8));
            break;
        
        default:
            break;
        }
    }

end:
    va_end(arg);
}

void kprintf_backspace()
{
    Display* curr_display = Display::SharedDisplay();
    curr_display->put_backspace();
}