#ifndef STRINGS_H
#define STRINGS_H

#include "stdint.h"

void itoa(uint8_t* buffer, uint32_t base, uint64_t value);
void reverse(char* s);
void backspace(char* s);
void append(char* s, char c);

size_t strlen(char* s);
int strcmp(char* s1, char* s2);
int strncmp(char* s1, char* s2, int bytes);
char* strcpy(char* dst, const char* src);

/*
    str -> the string to split (null-terminated)
    delim -> the character to split at
    count -> a pointer to the variable which keeps track of the number of slices
*/
char** split(const char* str, char delim, int* count);

#endif