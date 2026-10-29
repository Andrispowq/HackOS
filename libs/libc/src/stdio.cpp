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

static void Pad(Output& output, unsigned count, char ch = ' ')
{
    while (count)
    {
        output.Put(ch);
        --count;
    }
}

// Bound each field so an accidental huge width cannot monopolize a process.
static bool Decimal(const char*& format, unsigned& value)
{
    value = 0;
    while (*format >= '0' && *format <= '9')
    {
        value = value * 10 + (*format++ - '0');
        if (value > 1024)
        {
            return false;
        }
    }

    return true;
}

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

        bool left = false, zero = false;
        while (*format == '-' || *format == '0')
        {
            left = left || *format == '-';
            zero = zero || *format == '0';
            ++format;
        }

        unsigned width = 0, precision = 0;
        bool precise = false;
        if (!Decimal(format, width))
        {
            output.failed = true;
            break;
        }

        if (*format == '.')
        {
            ++format;
            precise = true;
            if (!Decimal(format, precision))
            {
                output.failed = true;
                break;
            }
        }

        if (!*format)
        {
            output.failed = true;
            break;
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

        ch = *format;
        if (ch)
        {
            ++format;
        }
        if (ch == '%' || ch == 'c')
        {
            if (!left)
            {
                Pad(output, width > 1 ? width - 1 : 0);
            }
            output.Put(ch == '%' ? '%' : (char)va_arg(args, int));
            if (left)
            {
                Pad(output, width > 1 ? width - 1 : 0);
            }
        }
        else if (ch == 's')
        {
            const char* text = va_arg(args, const char*);
            text = text ? text : "(null)";
            unsigned count = 0;
            while ((!precise || count < precision) && text[count])
            {
                ++count;
            }

            unsigned padding = width > count ? width - count : 0;
            if (!left)
            {
                Pad(output, padding);
            }
            for (unsigned i = 0; i < count; ++i)
            {
                output.Put(text[i]);
            }
            if (left)
            {
                Pad(output, padding);
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
            }
            else
            {
                value = length == 2 ? va_arg(args, unsigned long long)
                        : length    ? va_arg(args, unsigned long)
                                    : va_arg(args, unsigned int);
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

            if (precise && !precision && count == 1 && number[0] == '0')
            {
                count = 0;
            }

            unsigned zeros = precise && precision > count ? precision - count : 0;
            unsigned prefix = ch == 'p' ? 2 : negative ? 1 : 0;
            unsigned total = prefix + zeros + count;
            unsigned padding = width > total ? width - total : 0;
            if (!left && !(zero && !precise))
            {
                Pad(output, padding);
            }
            if (negative)
            {
                output.Put('-');
            }
            if (ch == 'p')
            {
                output.Put('0');
                output.Put('x');
            }
            if (!left && zero && !precise)
            {
                Pad(output, padding, '0');
            }
            Pad(output, zeros, '0');
            while (count)
            {
                output.Put(number[--count]);
            }
            if (left)
            {
                Pad(output, padding);
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
