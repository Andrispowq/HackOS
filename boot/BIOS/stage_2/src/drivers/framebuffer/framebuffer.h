#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "libc/stdint.h"

struct FramebufferInfo
{
    uint16_t width;
    uint16_t height;
    uint32_t video_memory;
    uint16_t bytes_per_line;
    uint8_t bpp;
    uint32_t bytes_per_pixel;
    uint16_t max_width;
    uint16_t max_height;
} __attribute__((packed));

#include "hackos/boot_info.h"

typedef BootFramebuffer FRAMEBUFFER;

FRAMEBUFFER PrepareFramebuffer(struct FramebufferInfo* info);

#endif
