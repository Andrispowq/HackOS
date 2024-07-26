#include "bitmap.h"

#define INDEX_FROM_BIT(a) (a / (8 * 8))
#define OFFSET_FROM_BIT(a) (a % (8 * 8))
    
bool Bitmap::Get(uint64_t index)
{
    uint64_t idx = INDEX_FROM_BIT(index);
    uint64_t off = OFFSET_FROM_BIT(index);
    return (buffer[idx] & (0x1 << off));
}

void Bitmap::Set(uint64_t index, bool value)
{
    uint64_t idx = INDEX_FROM_BIT(index);
    uint64_t off = OFFSET_FROM_BIT(index);

    if(value)
    {
        buffer[idx] |= (0x1 << off);
    }
    else
    {
        buffer[idx] &= ~(0x1 << off);
    }
}

uint64_t Bitmap::First()
{
    uint64_t i, j;
    for (i = 0; i < size; i++)
    {
        if (buffer[i] != 0xFFFFFFFFFFFFFFFF)
        {
            for (j = 0; j < 64; j++)
            {
                uint64_t toTest = 0x1 << j;
                if (!(buffer[i] & toTest))
                {
                    return i * 8 * 8 + j;
                }
            }
        }
    }

    return 0xFFFFFFFFFFFFFFFF;
}