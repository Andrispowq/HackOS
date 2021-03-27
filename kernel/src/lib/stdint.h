#ifndef STDINT_H
#define STDINT_H

typedef unsigned char       uint8_t;
typedef          char        int8_t;
typedef unsigned short      uint16_t;
typedef          short       int16_t;
typedef unsigned int        uint32_t;
typedef          int         int32_t;
typedef unsigned long long  uint64_t;
typedef          long long   int64_t;

typedef uint64_t size_t;

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

#define NULL 0
#define false 0
#define true 1

typedef uint8_t bool;

#endif