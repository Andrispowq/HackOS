#include "string.h"
#include "memory.h"

/**
 * K&R implementation
 */
void itoa(uint8_t* buffer, uint32_t base, uint64_t value) 
{
    uint8_t* p = buffer;
    uint8_t* p1, *p2;
    uint64_t ud = value;
    uint64_t divisor = 10;

    if(base == 'd' && value < 0) 
    {   
        *p++ = '-';
        buffer++;
        ud = -value;
    }
    else
    {
        if (base == 'x')
        {
            divisor = 16;
        }
    }

    do
    {
        uint64_t remainder = ud % divisor;
        *p++ = (remainder < 10) ? remainder + '0' : remainder + 'A' - 10;
    }
    while (ud /= divisor);

    *p = 0;
    p1 = buffer;
    p2 = p - 1;
    while (p1 < p2) 
    {
        uint8_t tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
        p1++;
        p2--;
    }
}

/* K&R */
void reverse(char s[]) 
{
    int c, i, j;
    for (i = 0, j = strlen(s) - 1; i < j; i++, j--) 
    {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* K&R */
int strlen(const char s[]) 
{
    int i = 0;
    while (s[i] != '\0') 
        ++i;

    return i;
}

void append(char s[], char n) 
{
    int len = strlen(s);
    s[len] = n;
    s[len + 1] = '\0';
}

void backspace(char s[]) 
{
    int len = strlen(s);
    s[len - 1] = '\0';
}

/* K&R 
 * Returns <0 if s1<s2, 0 if s1==s2, >0 if s1>s2 */
int strcmp(char s1[], char s2[]) 
{
    while (*s1 && (uint8_t)*s1 == (uint8_t)*s2) { ++s1; ++s2; }
    return (int)(uint8_t)*s1 - (int)(uint8_t)*s2;
}

int strncmp(char s1[], char s2[], int bytes)
{
    for (int i = 0; i < bytes; ++i)
    {
        int difference = (int)(uint8_t)s1[i] - (int)(uint8_t)s2[i];
        if (difference || s1[i] == 0) return difference;
    }
    return 0;
}

char* strcpy(char* dst, const char* src)
{
    char* start = dst;
    do { *dst++ = *src; } while (*src++);
    return start;
}

#include "hackos/runtime/split.h"

static void* split_allocate(size_t size)
{
    // The boot bump allocator does not align ordinary allocations. The shared
    // splitter needs pointer alignment for its array; boot kfree is a no-op.
    uint64_t padding = sizeof(char*) - 1;
    if (size > (uint64_t)-1 - padding) return 0;
    uint64_t address = kmalloc(size + padding);
    return (void*)((address + padding) & ~padding);
}

char** split(const char* str, char delim, int* count)
{
    return hackos_split(str, delim, count, split_allocate, kfree);
}
