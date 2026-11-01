#include <stddef.h>

void* memcpy(void* dst, const void* src, size_t size)
{
    unsigned char* d = dst;
    const unsigned char* s = src;
    for (size_t i = 0; i < size; ++i)
    {
        d[i] = s[i];
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t size)
{
    unsigned char* d = dst;
    const unsigned char* s = src;
    if (d < s)
    {
        for (size_t i = 0; i < size; ++i)
        {
            d[i] = s[i];
        }
    }
    else
    {
        while (size)
        {
            --size;
            d[size] = s[size];
        }
    }
    return dst;
}

void* memset(void* dst, int value, size_t size)
{
    unsigned char* d = dst;
    for (size_t i = 0; i < size; ++i)
    {
        d[i] = value;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t size)
{
    const unsigned char* x = a;
    const unsigned char* y = b;
    for (size_t i = 0; i < size; ++i)
    {
        if (x[i] != y[i])
        {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char* text)
{
    size_t size = 0;
    while (text[size])
    {
        ++size;
    }
    return size;
}
