#include "string.h"
#include "memory.h"

/**
 * K&R implementation
 */
void itoa(uint8_t* buf, uint32_t base, uint32_t d) 
{
    uint8_t* p = buf;
    uint8_t* p1, *p2;
    uint32_t ud = d;
    uint32_t divisor = 10;

    if(base == 'd' && d < 0) 
    {   
        *p++ = '-';
        buf++;
        ud = -d;
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
        uint32_t remainder = ud % divisor;
        *p++ = (remainder < 10) ? remainder + '0' : remainder + 'A' - 10;
    }
    while (ud /= divisor);

    *p = 0;
    p1 = buf;
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
    int i;
    for (i = 0; s1[i] == s2[i]; i++) 
    {
        if (s1[i] == '\0')
        {
            return 0;
        }
    }

    return s1[i] - s2[i];
}

int strncmp(char s1[], char s2[], int bytes)
{
    int i;
    for (i = 0; s1[i] == s2[i] && i < bytes; i++) 
    {
        if (s1[i] == '\0')
        {
            return 0;
        }
    }

    return s1[i] - s2[i];
}

char* strcpy(char* dst, const char* src)
{
    int length = strlen(src);
    int i; 
    for(i = 0; i < length; i++)
    {
        dst[i] = src[i];
    }

    return dst;
}

char** split(const char* str, char delim, int* count)
{
    int size = strlen(str);

    int cnt = 0;
    for(int i = 0; i < size; i++)
    {
        if(str[i] == delim)
        {
            cnt++;
        }
    }

    //Allocate the pointers, set the count
    char** ptr = (char**) kmalloc(cnt * sizeof(char*));
    *count = cnt;

    int idx = 0;
    int last_idx = 0;
    for(int i = 0; i < size; i++)
    {
        if(str[i] == delim)
        {
            int sz = i - last_idx;
            ptr[idx] = (char*) kmalloc(sz);
            memcpy(&ptr[idx], &str[last_idx], sz);
            last_idx = idx;
            idx++;
        }
    }

    return ptr;
}