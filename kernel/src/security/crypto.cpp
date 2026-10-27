#include "crypto.h"
#include "lib/memory.h"

void SecureErase(void* memory, uint64_t size)
{
    auto bytes = (volatile uint8_t*)memory;
    while (size--)
    {
        *bytes++ = 0;
    }
}

bool ConstantEqual(const void* left, const void* right, uint64_t size)
{
    auto a = (const uint8_t*)left;
    auto b = (const uint8_t*)right;
    uint8_t difference = 0;
    for (uint64_t i = 0; i < size; ++i)
    {
        difference |= a[i] ^ b[i];
    }

    return difference == 0;
}

namespace
{
uint32_t Rotate(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32 - count));
}

struct Hash
{
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64] = {};
    uint64_t bytes = 0;
    unsigned used = 0;

    void Transform()
    {
        static const uint32_t constants[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t words[64];
        for (unsigned i = 0; i < 16; ++i)
        {
            words[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                       (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        }

        for (unsigned i = 16; i < 64; ++i)
        {
            uint32_t a = words[i - 15], b = words[i - 2];
            words[i] = words[i - 16] + (Rotate(a, 7) ^ Rotate(a, 18) ^ (a >> 3)) + words[i - 7] +
                       (Rotate(b, 17) ^ Rotate(b, 19) ^ (b >> 10));
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (unsigned i = 0; i < 64; ++i)
        {
            uint32_t t1 =
                h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + words[i];
            uint32_t t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
        SecureErase(words, sizeof(words));
    }

    void Update(const void* input, uint64_t length)
    {
        auto data = (const uint8_t*)input;
        bytes += length;
        while (length--)
        {
            block[used++] = *data++;
            if (used == 64)
            {
                Transform();
                used = 0;
            }
        }
    }

    void Finish(uint8_t digest[32])
    {
        uint64_t bits = bytes * 8;
        uint8_t byte = 0x80;
        Update(&byte, 1);
        byte = 0;
        while (used != 56)
        {
            Update(&byte, 1);
        }

        uint8_t size[8];
        for (unsigned i = 0; i < 8; ++i)
        {
            size[7 - i] = bits >> (i * 8);
        }

        Update(size, 8);
        for (unsigned i = 0; i < 32; ++i)
        {
            digest[i] = state[i / 4] >> (24 - (i % 4) * 8);
        }

        SecureErase(this, sizeof(*this));
    }
};
}

void Sha256(const void* bytes, uint64_t size, uint8_t digest[32])
{
    Hash hash;
    hash.Update(bytes, size);
    hash.Finish(digest);
}

void Pbkdf2Sha256(const char* password, uint64_t length, const uint8_t* salt, uint64_t saltLength, uint32_t iterations,
                  uint8_t digest[32])
{
    uint8_t key[64] = {}, pad[64], value[32];
    if (length > 64)
    {
        Sha256(password, length, key);
    }
    else
    {
        memcpy(key, password, length);
    }

    Hash inner, outer;
    for (unsigned i = 0; i < 64; ++i)
    {
        pad[i] = key[i] ^ 0x36;
    }

    inner.Update(pad, 64);
    for (unsigned i = 0; i < 64; ++i)
    {
        pad[i] = key[i] ^ 0x5c;
    }

    outer.Update(pad, 64);
    Hash first = inner;
    first.Update(salt, saltLength);
    uint8_t blockIndex[4] = {0, 0, 0, 1};
    first.Update(blockIndex, 4);
    first.Finish(value);
    Hash last = outer;
    last.Update(value, 32);
    last.Finish(value);
    memcpy(digest, value, 32);
    for (uint32_t i = 1; i < iterations; ++i)
    {
        Hash in = inner, out = outer;
        in.Update(value, 32);
        in.Finish(value);
        out.Update(value, 32);
        out.Finish(value);
        for (unsigned j = 0; j < 32; ++j)
        {
            digest[j] ^= value[j];
        }
    }

    SecureErase(&inner, sizeof(inner));
    SecureErase(&outer, sizeof(outer));
    SecureErase(key, sizeof(key));
    SecureErase(pad, sizeof(pad));
    SecureErase(value, sizeof(value));
}
