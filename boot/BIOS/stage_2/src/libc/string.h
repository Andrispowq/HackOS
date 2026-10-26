#ifndef STRINGS_H
#define STRINGS_H

#include "stdint.h"

void itoa(uint8_t* buffer, uint32_t base, uint64_t value);
void reverse(char s[]);
int strlen(const char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(char s1[], char s2[]);
int strncmp(char s1[], char s2[], int bytes);
char* strcpy(char* dst, const char* src);

// Preserves empty fields, including empty input; returns count strings plus a
// null sentinel. Invalid input/failure returns null and sets count=0. Uses the
// boot bump allocator, whose kfree currently does not reclaim storage.
char** split(const char* str, char delim, int* count);

#endif
