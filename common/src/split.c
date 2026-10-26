#include "hackos/runtime/split.h"
#include <limits.h>

char** hackos_split(const char* str, char delim, int* count,
                    void* (*allocate)(size_t), void (*release)(void*))
{
    if (!count) return NULL;
    *count = 0;
    if (!str || !allocate || !release) return NULL;

    size_t fields = 1;
    for (const char* p = str; *p; ++p)
    {
        if (*p == delim)
        {
            if (fields == INT_MAX) return NULL;
            ++fields;
        }
    }
    if (fields > (size_t)-1 / sizeof(char*) - 1) return NULL;
    char** result = (char**)allocate((fields + 1) * sizeof(char*));
    if (!result) return NULL;

    size_t done = 0;
    const char* start = str;
    for (const char* p = str;; ++p)
    {
        if (*p && *p != delim) continue;
        size_t length = (size_t)(p - start);
        result[done] = (char*)allocate(length + 1);
        if (!result[done])
        {
            while (done) release(result[--done]);
            release(result);
            return NULL;
        }
        for (size_t i = 0; i < length; ++i) result[done][i] = start[i];
        result[done++][length] = '\0';
        if (!*p) break;
        start = p + 1;
    }
    result[done] = NULL;
    *count = (int)done;
    return result;
}
