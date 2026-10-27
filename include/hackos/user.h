#pragma once
#include "hackos/syscall.h"
#include "hackos/runtime/string.h"

namespace User
{
inline uint64_t Length(const char* s)
{
    return strlen((char*)s);
}

inline bool Equal(const char* a, const char* b)
{
    return strcmp((char*)a, (char*)b) == 0;
}

inline bool Starts(const char* a, const char* b)
{
    return strncmp((char*)a, (char*)b, Length(b)) == 0;
}

inline void Copy(char* to, const char* from)
{
    strcpy(to, from);
}

void Print(const char* text);
void Error(const char* text);
bool Write(uint64_t fd, const void* data, uint64_t length);
void Number(uint64_t value, unsigned base = 10);
}
