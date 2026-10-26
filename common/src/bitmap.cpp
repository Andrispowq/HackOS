#include "hackos/runtime/bitmap.h"

#define INDEX_FROM_BIT(a) (a / (8 * 8))
#define OFFSET_FROM_BIT(a) (a % (8 * 8))
    
bool Bitmap::Get(uint64_t index)
{
    if (index / 8 >= size) return true;
    uint64_t idx = INDEX_FROM_BIT(index);
    uint64_t off = OFFSET_FROM_BIT(index);
    return (buffer[idx] & (1ULL << off));
}

void Bitmap::Set(uint64_t index, bool value)
{
    if (index / 8 >= size) return;
    uint64_t idx = INDEX_FROM_BIT(index);
    uint64_t off = OFFSET_FROM_BIT(index);

    if(value)
    {
        buffer[idx] |= (1ULL << off);
    }
    else
    {
        buffer[idx] &= ~(1ULL << off);
    }
}

uint64_t Bitmap::First()
{
    uint64_t i, j;
    for (i = 0; i < size / sizeof(uint64_t); i++)
    {
        if (buffer[i] != 0xFFFFFFFFFFFFFFFF)
        {
            for (j = 0; j < 64; j++)
            {
                uint64_t toTest = 1ULL << j;
                if (!(buffer[i] & toTest))
                {
                    return i * 8 * 8 + j;
                }
            }
        }
    }

    return 0xFFFFFFFFFFFFFFFF;
}