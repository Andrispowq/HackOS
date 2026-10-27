#pragma once
#include "hackos/types.h"

constexpr uint32_t PASSWORD_ITERATIONS = 600000;
void SecureErase(void* memory, uint64_t size);
bool ConstantEqual(const void* left, const void* right, uint64_t size);
void Sha256(const void* bytes, uint64_t size, uint8_t digest[32]);
void Pbkdf2Sha256(const char* password, uint64_t length, const uint8_t* salt, uint64_t saltLength, uint32_t iterations,
                  uint8_t digest[32]);
bool RandomSalt(uint8_t salt[16]);
