#include "string.h"
#include "memory.h"

#include "hackos/runtime/split.h"

static void* split_allocate(size_t size)
{
    return (void*)malloc(size);
}

char** split(const char* str, char delim, int* count)
{
    return hackos_split(str, delim, count, split_allocate, free);
}
