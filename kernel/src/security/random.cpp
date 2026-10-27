#include "crypto.h"

bool RandomSalt(uint8_t salt[16])
{
    uint32_t a = 1, b, c = 0, d;
    asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (!(c & (1U << 30)))
    {
        return false;
    }

    for (unsigned i = 0; i < 2; ++i)
    {
        uint64_t value = 0;
        uint8_t ready = 0;
        for (unsigned retry = 0; retry < 10 && !ready; ++retry)
        {
            asm volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ready)::"cc");
        }

        if (!ready)
        {
            SecureErase(salt, 16);
            return false;
        }

        for (unsigned j = 0; j < 8; ++j)
        {
            salt[i * 8 + j] = value >> (j * 8);
        }

        value = 0;
    }

    return true;
}
