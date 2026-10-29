#include <initializer_list>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <climits>

int hackos_snprintf(char*, size_t, const char*, ...);

uint64_t syscall_user_write(const char*, uint64_t length)
{
    return length;
}

uint64_t syscall_console(uint64_t)
{
    return 0;
}

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::fprintf(stderr, "Formatting failed at line %d\n", __LINE__);                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main()
{
    char actual[128], expected[128];
    const char* integerFormats[] = {"%d", "%8d", "%-8d", "%08d", "%08.4d", "%8.0d", "%-08d", "%0d"};
    for (const char* format : integerFormats)
    {
        for (int value : {0, 7, -7, INT_MIN, INT_MAX})
        {
            int count = std::snprintf(expected, sizeof(expected), format, value);
            CHECK(hackos_snprintf(actual, sizeof(actual), format, value) == count);
            CHECK(std::strcmp(actual, expected) == 0);
        }
    }

    const char* stringFormats[] = {"%s", "%12s", "%-12s", "%.3s", "%12.3s", "%-12.3s", "%.0s"};
    for (const char* format : stringFormats)
    {
        int count = std::snprintf(expected, sizeof(expected), format, "abcdef");
        CHECK(hackos_snprintf(actual, sizeof(actual), format, "abcdef") == count);
        CHECK(std::strcmp(actual, expected) == 0);
    }

    CHECK(hackos_snprintf(actual, sizeof(actual), "%016llx", 0x123ULL) == 16);
    CHECK(std::strcmp(actual, "0000000000000123") == 0);
    CHECK(hackos_snprintf(actual, sizeof(actual), "%020lld", LLONG_MIN) == 20);
    CHECK(std::strcmp(actual, "-9223372036854775808") == 0);
    CHECK(hackos_snprintf(actual, sizeof(actual), "%8c", 'x') == 8);
    CHECK(std::strcmp(actual, "       x") == 0);
    CHECK(hackos_snprintf(actual, sizeof(actual), "%08p", (void*)0x123) == 8);
    CHECK(std::strcmp(actual, "0x000123") == 0);
    CHECK(hackos_snprintf(actual, sizeof(actual), "%4s", (const char*)nullptr) == 6);
    CHECK(std::strcmp(actual, "(null)") == 0);
    CHECK(hackos_snprintf(nullptr, 0, "%1024u", 1U) == 1024);

    for (size_t capacity = 0; capacity <= 12; ++capacity)
    {
        std::memset(actual, '!', sizeof(actual));
        CHECK(hackos_snprintf(actual + 1, capacity, "%08d %s", -7, "abcdef") == 15);
        CHECK(actual[0] == '!' && actual[capacity + 1] == '!');
        if (capacity)
        {
            CHECK(actual[capacity] == 0);
        }
    }

    const char* invalid[] = {"%", "%l", "%1025s", "%.1025s", "%999999999999999999999u", "%*s", "%f", "%q"};
    for (const char* format : invalid)
    {
        CHECK(hackos_snprintf(actual, sizeof(actual), format) == -1);
        CHECK(actual[0] == 0);
    }

    std::puts("Integer/string columns, precision, limits and truncation verified.");
    return 0;
}
