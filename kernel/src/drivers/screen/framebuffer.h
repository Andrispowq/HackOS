#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/stdint.h"

typedef struct _framebuffer
{
    uint32_t* address;
    uint32_t width, height, bpp, pitch;
} FRAMEBUFFER;

#endif