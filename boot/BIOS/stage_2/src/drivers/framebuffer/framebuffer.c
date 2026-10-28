#include "framebuffer.h"

FRAMEBUFFER PrepareFramebuffer(struct FramebufferInfo* info)
{
    FRAMEBUFFER fb;

    fb.address = (uint32_t*)(uint64_t)info->video_memory;
    fb.width = (uint32_t)info->width;
    fb.height = (uint32_t)info->height;
    fb.bpp = (uint32_t)info->bpp;
    fb.pitch = (uint32_t)info->bytes_per_line / sizeof(uint32_t);

    // The loader already identity-maps the 32-bit VBE framebuffer address.
    return fb;
}
