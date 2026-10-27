#include "hackos/stdio.h"
#include "hackos/user.h"

struct Output
{
    char* buffer;
    size_t capacity, count = 0;
    bool console;
    char pending[256];
    size_t used = 0;
    bool failed = false;

    void Flush()
    {
        if (used && syscall_user_write(pending, used) != used)
        {
            failed = true;
        }

        used = 0;
    }

    void Put(char ch)
    {
        if (console)
        {
            pending[used++] = ch;
            if (used == sizeof(pending))
            {
                Flush();
            }
        }
        else if (capacity && count < capacity - 1)
        {
            buffer[count] = ch;
        }

        ++count;
    }
};

static int Format(Output& output, const char* format, va_list args)
{
    while (*format)
    {
        char ch = *format++;
        if (ch != '%')
        {
            output.Put(ch);
            continue;
        }

        unsigned length = 0;
        while (*format == 'l' && length < 2)
        {
            ++length;
            ++format;
        }

        if (*format == 'z')
        {
            length = 1;
            ++format;
        }

        ch = *format++;
        if (ch == '%' || ch == 'c')
        {
            output.Put(ch == '%' ? '%' : (char)va_arg(args, int));
        }
        else if (ch == 's')
        {
            const char* text = va_arg(args, const char*);
            text = text ? text : "(null)";
            while (*text)
            {
                output.Put(*text++);
            }
        }
        else if (ch == 'd' || ch == 'i' || ch == 'u' || ch == 'x' || ch == 'X' || ch == 'p')
        {
            bool negative = false;
            uint64_t value;
            if (ch == 'd' || ch == 'i')
            {
                int64_t signedValue = length == 2 ? va_arg(args, long long)
                                      : length    ? va_arg(args, long)
                                                  : va_arg(args, int);
                negative = signedValue < 0;
                value = negative ? 0ULL - (uint64_t)signedValue : (uint64_t)signedValue;
            }
            else if (ch == 'p')
            {
                value = (uint64_t)va_arg(args, void*);
                output.Put('0');
                output.Put('x');
            }
            else
            {
                value = length == 2 ? va_arg(args, unsigned long long)
                        : length    ? va_arg(args, unsigned long)
                                    : va_arg(args, unsigned int);
            }

            if (negative)
            {
                output.Put('-');
            }

            unsigned base = ch == 'x' || ch == 'X' || ch == 'p' ? 16 : 10;
            const char* digits = ch == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char number[32];
            unsigned count = 0;
            do
            {
                number[count++] = digits[value % base];
                value /= base;
            } while (value);

            while (count)
            {
                output.Put(number[--count]);
            }
        }
        else
        {
            output.failed = true;
            break;
        }
    }

    if (output.console)
    {
        output.Flush();
    }
    else if (output.capacity)
    {
        output.buffer[output.count < output.capacity ? output.count : output.capacity - 1] = 0;
    }

    return output.failed || output.count > 0x7fffffff ? -1 : (int)output.count;
}

int vsnprintf(char* buffer, size_t capacity, const char* format, va_list arguments)
{
    if ((!buffer && capacity) || !format)
    {
        return -1;
    }

    Output output = {buffer, capacity, 0, false, {}, 0, false};
    va_list copy;
    va_copy(copy, arguments);
    int result = Format(output, format, copy);
    va_end(copy);
    return result;
}

int snprintf(char* buffer, size_t capacity, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return result;
}

int printf(const char* format, ...)
{
    if (!format)
    {
        return -1;
    }

    Output output = {nullptr, 0, 0, true, {}, 0, false};
    va_list args;
    va_start(args, format);
    int result = Format(output, format, args);
    va_end(args);
    return result;
}

void printf_backspace()
{
    syscall_console(0);
}
