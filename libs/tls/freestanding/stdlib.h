#pragma once
#include <stddef.h>
void* malloc(size_t);
void free(void*);
int posix_memalign(void**, size_t, size_t);
