#pragma once
#include "hackos/types.h"

extern "C" void* malloc(size_t size);
extern "C" void* calloc(size_t count, size_t size);
extern "C" void* realloc(void* pointer, size_t size);
extern "C" void free(void* pointer);
