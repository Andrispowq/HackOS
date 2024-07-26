#include "framebuffer.h"
#include "cpu/paging/paging.h"

FRAMEBUFFER PrepareFramebuffer(struct FramebufferInfo* info)
{
    FRAMEBUFFER fb;

    fb.address = (uint32_t*)(uint64_t)info->video_memory;
    fb.width = (uint32_t)info->width;
    fb.height = (uint32_t)info->height;
    fb.bpp = (uint32_t)info->bpp;
    fb.pitch = (uint32_t)info->bytes_per_line;

    //Identity map the framebuffer's memory, so we can use it
    uint64_t ptr = (uint64_t)info->video_memory;
    uint64_t size = (uint64_t)info->width * info->height * info->bpp / 8;
    for(uint64_t i = ptr; i < (ptr + size + 0x1000); i += 0x1000)
    {
        MapMemory(i, i);
    }

    return fb;
}