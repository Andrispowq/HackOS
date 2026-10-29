#include "framebuffer.h"
#include "lib/memory.h"

void Framebuffer::SetClip(const FramebufferDamage& region)
{
    clipped = true;
    clip = {};
    if (region.right > region.left && region.bottom > region.top)
    {
        clip.Include(region.left, region.top, region.right - region.left, region.bottom - region.top, width, height);
    }
}

void Framebuffer::ClearClip()
{
    clipped = false;
}

bool Framebuffer::ClipRect(uint32_t& x, uint32_t& y, uint32_t& columns, uint32_t& rows) const
{
    if (!columns || !rows || x >= width || y >= height)
    {
        return false;
    }

    uint32_t right = x + (columns < width - x ? columns : width - x);
    uint32_t bottom = y + (rows < height - y ? rows : height - y);
    if (clipped)
    {
        if (x < clip.left)
        {
            x = clip.left;
        }
        if (y < clip.top)
        {
            y = clip.top;
        }
        if (right > clip.right)
        {
            right = clip.right;
        }
        if (bottom > clip.bottom)
        {
            bottom = clip.bottom;
        }
    }

    if (x >= right || y >= bottom)
    {
        return false;
    }

    columns = right - x;
    rows = bottom - y;
    return true;
}

void Framebuffer::PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    if ((x >= width) || (y >= height) ||
        (clipped && (x < clip.left || x >= clip.right || y < clip.top || y >= clip.bottom)))
    {
        return;
    }

    address[(uint64_t)y * pitch + x] = colour;
}

uint32_t Framebuffer::GetPixel(uint32_t x, uint32_t y)
{
    if ((x >= width) || (y >= height))
    {
        return 0x0;
    }

    return address[(uint64_t)y * pitch + x];
}

void Framebuffer::DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    if (!ClipRect(x, y, width, height))
    {
        return;
    }

    uint32_t x_end = x + width;
    uint32_t y_end = y + height;
    for (uint32_t row = y; row < y_end; ++row)
    {
        for (uint32_t column = x; column < x_end; ++column)
        {
            address[(uint64_t)row * pitch + column] = colour;
        }
    }
}

void Framebuffer::Clear(uint32_t colour)
{
    DrawRect(0, 0, width, height, colour);
}

bool MouseDrawn = false;
uint32_t MouseCursorBuffer[16 * 16];
uint32_t MouseCursorBufferAfter[16 * 16];

void Framebuffer::DrawMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y, uint32_t colour)
{
    int xMax = 16;
    int yMax = 16;
    int differenceX = width - x;
    int differenceY = height - y;

    if (differenceX < 16)
    {
        xMax = differenceX;
    }

    if (differenceY < 16)
    {
        yMax = differenceY;
    }

    for (int _y = 0; _y < yMax; _y++)
    {
        for (int _x = 0; _x < xMax; _x++)
        {
            int bit = _y * 16 + _x;
            int byte = bit / 8;
            if ((cursor[byte] & (0b10000000 >> (_x % 8))))
            {
                MouseCursorBuffer[_x + _y * 16] = GetPixel(x + _x, y + _y);
                PutPixel(x + _x, y + _y, colour);
                MouseCursorBufferAfter[_x + _y * 16] = GetPixel(x + _x, y + _y);
            }
        }
    }

    MouseDrawn = true;
}

void Framebuffer::ClearMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y)
{
    if (!MouseDrawn)
    {
        return;
    }

    int xMax = 16;
    int yMax = 16;
    int differenceX = width - x;
    int differenceY = height - y;

    if (differenceX < 16)
    {
        xMax = differenceX;
    }

    if (differenceY < 16)
    {
        yMax = differenceY;
    }

    for (int _y = 0; _y < yMax; _y++)
    {
        for (int _x = 0; _x < xMax; _x++)
        {
            int bit = _y * 16 + _x;
            int byte = bit / 8;
            if ((cursor[byte] & (0b10000000 >> (_x % 8))))
            {
                if (GetPixel(x + _x, y + _y) == MouseCursorBufferAfter[_x + _y * 16])
                {
                    PutPixel(x + _x, y + _y, MouseCursorBuffer[_x + _y * 16]);
                }
            }
        }
    }
}

void Framebuffer::CopyFrom(const Framebuffer& source, uint32_t x, uint32_t y)
{
    uint32_t originalX = x, originalY = y;
    uint32_t columns = source.width, rows = source.height;
    if (!ClipRect(x, y, columns, rows))
    {
        return;
    }

    for (uint32_t row = 0; row < rows; row++)
    {
        uint32_t* destination = address + (uint64_t)(y + row) * pitch + x;
        const uint32_t* pixels = source.address + (uint64_t)(row + y - originalY) * source.pitch + x - originalX;
#if defined(__x86_64__)
        // SSE2 is part of the kernel's saved context. Unaligned loads/stores
        // support clipped windows and padded scanlines without over-reading.
        uint32_t remaining = columns;
        while (remaining >= 16)
        {
            asm volatile("movdqu 0(%1), %%xmm0\n\t"
                         "movdqu 16(%1), %%xmm1\n\t"
                         "movdqu 32(%1), %%xmm2\n\t"
                         "movdqu 48(%1), %%xmm3\n\t"
                         "movdqu %%xmm0, 0(%0)\n\t"
                         "movdqu %%xmm1, 16(%0)\n\t"
                         "movdqu %%xmm2, 32(%0)\n\t"
                         "movdqu %%xmm3, 48(%0)"
                         :
                         : "r"(destination), "r"(pixels)
                         : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
            destination += 16;
            pixels += 16;
            remaining -= 16;
        }

        uint64_t count = remaining;
        asm volatile("cld\n\t"
                     "rep movsl"
                     : "+D"(destination), "+S"(pixels), "+c"(count)
                     :
                     : "memory", "cc");
#else
        memcpy(destination, pixels, (uint64_t)columns * sizeof(uint32_t));
#endif
    }
}

void Framebuffer::CopyRegion(const Framebuffer& source, uint32_t x, uint32_t y, uint32_t columns, uint32_t rows)
{
    if (x >= width || y >= height || x >= source.width || y >= source.height)
    {
        return;
    }

    Framebuffer region = source;
    region.address += (uint64_t)y * source.pitch + x;
    region.width = columns < source.width - x ? columns : source.width - x;
    region.height = rows < source.height - y ? rows : source.height - y;
    CopyFrom(region, x, y);
}

static bool PixelsDiffer(const uint32_t* current, const uint32_t* saved, uint32_t count)
{
#if defined(__x86_64__)
    if (count == 32)
    {
        uint32_t mask;
        // Fixed blocks avoid a branch for every four pixels in the common
        // unchanged case. Every load stays within this complete 32-pixel span.
        asm volatile("movdqu 0(%1), %%xmm1\n\t"
                     "movdqu 0(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "movdqa %%xmm1, %%xmm0\n\t"
                     "movdqu 16(%1), %%xmm1\n\t"
                     "movdqu 16(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 32(%1), %%xmm1\n\t"
                     "movdqu 32(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 48(%1), %%xmm1\n\t"
                     "movdqu 48(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 64(%1), %%xmm1\n\t"
                     "movdqu 64(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 80(%1), %%xmm1\n\t"
                     "movdqu 80(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 96(%1), %%xmm1\n\t"
                     "movdqu 96(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "movdqu 112(%1), %%xmm1\n\t"
                     "movdqu 112(%2), %%xmm2\n\t"
                     "pcmpeqd %%xmm2, %%xmm1\n\t"
                     "pand %%xmm1, %%xmm0\n\t"
                     "pmovmskb %%xmm0, %0"
                     : "=r"(mask)
                     : "r"(current), "r"(saved)
                     : "xmm0", "xmm1", "xmm2", "memory");
        return mask != 0xffff;
    }
#endif
    for (uint32_t i = 0; i < count; ++i)
    {
        if (current[i] != saved[i])
        {
            return true;
        }
    }

    return false;
}

FramebufferDamage Framebuffer::ChangedBounds(const Framebuffer& previous) const
{
    FramebufferDamage result;
    if (width != previous.width || height != previous.height || !previous.address)
    {
        result.Include(0, 0, width, height, width, height);
        return result;
    }

    for (uint32_t y = 0; y < height; ++y)
    {
        uint32_t first = width, last = 0;
        for (uint32_t x = 0; x < width;)
        {
            uint32_t count = width - x < 32 ? width - x : 32;
            if (PixelsDiffer(address + (uint64_t)y * pitch + x, previous.address + (uint64_t)y * previous.pitch + x,
                             count))
            {
                if (first == width)
                {
                    first = x;
                }
                last = x + count;
            }
            x += count;
        }
        if (first < last)
        {
            result.Include(first, y, last - first, 1, width, height);
        }
    }

    return result;
}

uint64_t Framebuffer::PresentChanged(const Framebuffer& source, Framebuffer& previous, bool force)
{
    if (source.width != width || source.height != height || previous.width != width || previous.height != height)
    {
        return 0;
    }

    uint64_t pixels = 0;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint32_t* current = source.address + (uint64_t)y * source.pitch;
        uint32_t* saved = previous.address + (uint64_t)y * previous.pitch;
        // Group differences into 32-pixel spans to bound comparisons and copies.
        uint32_t start = width;
        for (uint32_t x = 0; x < width;)
        {
            uint32_t count = width - x < 32 ? width - x : 32;
            bool changed = force || PixelsDiffer(current + x, saved + x, count);

            if (changed && start == width)
            {
                start = x;
            }

            uint32_t end = x + count;
            if (start != width && (!changed || end == width))
            {
                uint32_t length = (changed ? end : x) - start;
                CopyRegion(source, start, y, length, 1);
                previous.CopyRegion(source, start, y, length, 1);
                pixels += length;
                start = width;
            }

            x = end;
        }
    }

    return pixels;
}

void FramebufferDamage::Include(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t screenWidth,
                                uint32_t screenHeight)
{
    if (!width || !height || x >= screenWidth || y >= screenHeight)
    {
        return;
    }

    uint32_t endX = x + (width < screenWidth - x ? width : screenWidth - x);
    uint32_t endY = y + (height < screenHeight - y ? height : screenHeight - y);
    if (Empty())
    {
        left = x;
        top = y;
        right = endX;
        bottom = endY;
    }
    else
    {
        if (x < left)
        {
            left = x;
        }
        if (y < top)
        {
            top = y;
        }
        if (endX > right)
        {
            right = endX;
        }
        if (endY > bottom)
        {
            bottom = endY;
        }
    }
}

void FramebufferDamageList::Include(const FramebufferDamage& region)
{
    if (region.Empty())
    {
        return;
    }

    FramebufferDamage merged = region;
    for (unsigned i = 0; i < count;)
    {
        const auto& other = regions[i];
        if (merged.left <= other.right && other.left <= merged.right && merged.top <= other.bottom &&
            other.top <= merged.bottom)
        {
            if (other.left < merged.left)
            {
                merged.left = other.left;
            }
            if (other.top < merged.top)
            {
                merged.top = other.top;
            }
            if (other.right > merged.right)
            {
                merged.right = other.right;
            }
            if (other.bottom > merged.bottom)
            {
                merged.bottom = other.bottom;
            }
            regions[i] = regions[--count];
            i = 0;
        }
        else
        {
            ++i;
        }
    }

    if (count == 16)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            const auto& other = regions[i];
            if (other.left < merged.left)
            {
                merged.left = other.left;
            }
            if (other.top < merged.top)
            {
                merged.top = other.top;
            }
            if (other.right > merged.right)
            {
                merged.right = other.right;
            }
            if (other.bottom > merged.bottom)
            {
                merged.bottom = other.bottom;
            }
        }
        count = 0;
    }
    regions[count++] = merged;
}
