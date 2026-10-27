#pragma once
#include "lib/stdint.h"

extern "C" int printf(const char*, ...);
extern "C" void* test_alloc(size_t);
extern "C" void test_free(void*);
extern "C" void test_copy(void*, const void*, size_t);
extern "C" void test_fill(void*, int, size_t);
extern "C" int test_equal(const void*, const void*, size_t);
extern "C" int test_name_equal(const char*, const char*);
extern "C" void test_abort(const char*);
extern "C" void* test_disk_load(size_t*);
extern "C" void test_disk_save(const void*, size_t);

struct TestCase
{
    const char* name;
    int (*run)();
};

extern TestCase test_cases[256];
extern unsigned test_count;

struct RegisterTest
{
    RegisterTest(const char* name, int (*run)())
    {
        if (test_count == 256)
        {
            test_abort("test registry overflow");
        }

        test_cases[test_count++] = {name, run};
    }
};

#define TEST(name)                                                                                                     \
    static int name();                                                                                                 \
    static RegisterTest reg_##name(#name, name);                                                                       \
    static int name()
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

extern bool fromUEFI;
extern uint8_t heap_init;
extern unsigned test_mapping_count;
