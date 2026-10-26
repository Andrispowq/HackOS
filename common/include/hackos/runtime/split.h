#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
// Same ownership and empty-field contract as split(); allocator pair is local
// to the consumer. No kernel services or hosted runtime are used here.
char** hackos_split(const char* str, char delim, int* count,
                    void* (*allocate)(size_t), void (*release)(void*));
#ifdef __cplusplus
}
#endif
