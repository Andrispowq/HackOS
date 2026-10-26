#pragma once
#include "hackos/types.h"

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int memcmp(void* a, void* b, size_t n);
