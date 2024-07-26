#include "string.h"
#include "memory.h"

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

void reverse(char* s) 
{
    int c, i, j;
    for (i = 0, j = strlen(s) - 1; i < j; i++, j--) 
    {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void append(char* s, char n) 
{
    size_t len = strlen(s);
    s[len] = n;
    s[len + 1] = '\0';
}

void backspace(char* s) 
{
    size_t len = strlen(s);
    s[len - 1] = '\0';
}

extern "C" size_t __strlen(char* s);
extern "C" int __strcmp(char* s1, char* s2);
extern "C" int __strncmp(char* s1, char* s2, size_t bytes);
extern "C" char* __strcpy(char* dst, const char* src);

size_t strlen(char* s) 
{
    return __strlen(s);
}

int strcmp(char* s1, char* s2) 
{
    size_t cnt = 0;
    while(*s1 != 0)
    {
        if(*s1 != *s2)
        {
            break;
        }

        s1++;
        s2++;
    }

    return *s1 - *s2;
    //return __strcmp(s1, s2);
}

int strncmp(char* s1, char* s2, int bytes)
{
    return (__strncmp(s1, s2, bytes) == 0);
}

char* strcpy(char* dst, const char* src)
{
    return __strcpy(dst, src);
}

char** split(const char* str, char delim, int* count)
{
    size_t size = strlen((char*)str);

    size_t cnt = 0;
    for(size_t i = 0; i < size; i++)
    {
        if(str[i] == delim)
        {
            cnt++;
        }
    }

    //Allocate the pointers, set the count
    char** ptr = (char**) kmalloc(cnt * sizeof(char*));
    *count = cnt;

    size_t idx = 0;
    size_t last_idx = 0;
    for(size_t i = 0; i < size; i++)
    {
        if(str[i] == delim)
        {
            size_t sz = i - last_idx;
            ptr[idx] = (char*) kmalloc(sz);
            memcpy(&ptr[idx], &str[last_idx], sz);
            last_idx = idx;
            idx++;
        }
    }

    return ptr;
}