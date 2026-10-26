#ifndef STRINGS_H
#define STRINGS_H

#include "hackos/types.h"

void itoa(uint8_t* buffer, uint32_t base, uint64_t value);
void reverse(char* s);
void backspace(char* s);
void append(char* s, char c);

size_t strlen(char* s);
int strcmp(char* s1, char* s2);
int strncmp(char* s1, char* s2, int bytes);
char* strcpy(char* dst, const char* src);

// Preserves empty fields (including empty input), returns count owned strings
// plus a null sentinel. Free each string and then the array with the caller's
// allocator. Invalid input or allocation failure returns null and sets count=0.
char** split(const char* str, char delim, int* count);

#endif
