#include "hackos/user.h"
#include "hackos/stdlib.h"
#include "hackos/stdio.h"
#include "hackos/runtime/memory.h"
#include "hackos/runtime/string.h"
using namespace User;

extern "C"
{
    volatile uint64_t usability_counter;
    volatile uint64_t usability_release;
}

static int Fail(unsigned line)
{
    Print("PROBE_FAIL usability ");
    Number(line);
    Print("\n");
    return 1;
}

#define REQUIRE(test)                                                                                                  \
    if (!(test))                                                                                                       \
    {                                                                                                                  \
        return Fail(__LINE__);                                                                                         \
    }

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        REQUIRE(syscall_terminal_read(2) == HACKOS_ERROR);
        REQUIRE(syscall_job_control(1, HACKOS_JOB_FOREGROUND) == HACKOS_DENIED);
        REQUIRE(syscall_job_wait(1, nullptr, 0) == HACKOS_ERROR);
        REQUIRE(syscall_heap_resize(HACKOS_HEAP_LIMIT + 1) == HACKOS_ERROR);
        REQUIRE(syscall_heap_resize(~0ULL) == HACKOS_ERROR);
        REQUIRE(syscall_heap_resize(8192) == HACKOS_HEAP_BASE);
        auto raw = (volatile uint8_t*)HACKOS_HEAP_BASE;
        REQUIRE(raw[4096] == 0);
        raw[4096] = 77;
        REQUIRE(syscall_heap_resize(0) == HACKOS_HEAP_BASE);
        REQUIRE(syscall_user_write((const char*)raw, 1) == HACKOS_ERROR);
        REQUIRE(syscall_heap_resize(8192) == HACKOS_HEAP_BASE);
        REQUIRE(raw[4096] == 0);
        REQUIRE(syscall_heap_resize(0) == HACKOS_HEAP_BASE);
        REQUIRE(!malloc(0) && !malloc(~(size_t)0) && !calloc(~(size_t)0, 2));
        auto memory = (uint8_t*)calloc(100, 100);
        REQUIRE(memory && !((uint64_t)memory & 15));
        for (unsigned i = 0; i < 10000; ++i)
        {
            REQUIRE(memory[i] == 0);
            memory[i] = i;
        }

        auto replacement = (uint8_t*)realloc(memory, 40000);
        REQUIRE(replacement);
        for (unsigned i = 0; i < 10000; ++i)
        {
            REQUIRE(replacement[i] == (uint8_t)i);
        }

        REQUIRE(!realloc(replacement, HACKOS_HEAP_LIMIT + 1) && replacement[9999] == (uint8_t)9999);
        free(replacement);
        void* blocks[128];
        unsigned count = 0;
        while (count < 128 && (blocks[count] = malloc(8192)))
        {
            ++count;
        }

        REQUIRE(count > 40 && count < 128);
        for (unsigned i = 0; i < count; ++i)
        {
            free(blocks[i]);
        }

        auto whole = (uint8_t*)malloc(HACKOS_HEAP_LIMIT - 32);
        REQUIRE(whole && !malloc(1));
        whole[HACKOS_HEAP_LIMIT - 33] = 42;
        free(whole);
        auto object = new uint64_t[32];
        object[31] = 123;
        REQUIRE(object[31] == 123);
        delete[] object;
        int words = 0;
        char** parts = split("one:two:three", ':', &words);
        REQUIRE(parts && words == 3 && Equal(parts[1], "two"));
        for (int i = 0; i < words; ++i)
        {
            free(parts[i]);
        }

        free(parts);
        char formatted[128];
        int n = snprintf(formatted, sizeof(formatted), "%s %c %d %llu %x %%", "text", '!', -7, 18446744073709551615ULL,
                         0xab);
        REQUIRE(n == 35 && Equal(formatted, "text ! -7 18446744073709551615 ab %"));
        REQUIRE(snprintf(formatted, 5, "%s", "abcdef") == 6 && Equal(formatted, "abcd"));
        REQUIRE(snprintf(nullptr, 0, "%lld", (-9223372036854775807LL - 1)) == 20);
        REQUIRE(snprintf(formatted, sizeof(formatted), "%q") == -1);
        REQUIRE(snprintf(formatted, sizeof(formatted), "%06d %-5s %04x", -7, "hi", 0xab) == 17);
        REQUIRE(Equal(formatted, "-00007 hi    00ab"));
        REQUIRE(snprintf(formatted, sizeof(formatted), "%.3s %8.4u", "abcdef", 12u) == 12);
        REQUIRE(Equal(formatted, "abc     0012"));
        REQUIRE(snprintf(formatted, 5, "%08u", 12u) == 8 && Equal(formatted, "0000"));
        REQUIRE(snprintf(formatted, sizeof(formatted), "%1025u", 1u) == -1);
        REQUIRE(snprintf(formatted, sizeof(formatted), "%") == -1);
        REQUIRE(printf("USABILITY_RUNTIME_OK %u\n", 17u) == 24);
        return 0;
    }

    if (Equal(argv[1], "controller"))
    {
        const char* args[] = {argv[0], "child"};
        REQUIRE(syscall_spawn_job("/usr/bin/usabprobe", 2, args, 2) == HACKOS_ERROR);
        REQUIRE(syscall_spawn_job((const char*)1, 2, args, 0) == HACKOS_ERROR);
        uint64_t child = syscall_spawn_job("/usr/bin/usabprobe", 2, args, 0);
        REQUIRE(child != HACKOS_ERROR && child != HACKOS_DENIED);
        REQUIRE(syscall_job_control(child, HACKOS_JOB_QUERY) == 1);
        REQUIRE(syscall_job_control(child, 4) == HACKOS_ERROR);
        REQUIRE(syscall_job_wait(child, (int64_t*)1, 0) == HACKOS_ERROR);
        REQUIRE(syscall_job_wait(child, (int64_t*)main, 0) == HACKOS_ERROR);
        REQUIRE(syscall_job_wait(child, nullptr, 2) == HACKOS_ERROR);
        REQUIRE(syscall_job_wait(child, nullptr, HACKOS_WAIT_NOHANG) == 0);
        REQUIRE(syscall_job_control(child, HACKOS_JOB_TERMINATE) == 0);
        int64_t status = 0;
        REQUIRE(syscall_job_wait(child, &status, 0) == child && status == 137);
        REQUIRE(syscall_job_wait(child, nullptr, 0) == HACKOS_ERROR);
        Print("USABILITY_CONTROLLER_OK\n");
        return 0;
    }

    if (Equal(argv[1], "heapfault"))
    {
        auto memory = (uint8_t*)malloc(16);
        REQUIRE(memory);
        memory[0] = 0xc3;
        ((void (*)())memory)();
        return Fail(__LINE__);
    }

    if (Equal(argv[1], "read"))
    {
        REQUIRE(syscall_terminal_read(HACKOS_INPUT_NONBLOCK) == 0);
        Print("USABILITY_READ_READY\n");
        REQUIRE(syscall_terminal_read(0) == 'k');
        Print("USABILITY_READ_OK\n");
        return 0;
    }

    if (Equal(argv[1], "background"))
    {
        REQUIRE(syscall_terminal_read(0) == HACKOS_DENIED);
        Print("USABILITY_BACKGROUND_DENIED\n");
        return 0;
    }

    REQUIRE(malloc(12000)); // intentionally left for exit/fault/kill reclamation
    REQUIRE(syscall_open("/usr/hello.txt") != HACKOS_ERROR);
    if (Equal(argv[1], "tree") || Equal(argv[1], "orphan"))
    {
        const char* args[] = {argv[0], "child"};
        uint64_t child = syscall_spawn_foreground("/usr/bin/usabprobe", 2, args);
        REQUIRE(child != HACKOS_ERROR && child != HACKOS_DENIED);
        Print("USABILITY_PARENT_READY\n");
        if (Equal(argv[1], "orphan"))
        {
            return 0;
        }

        REQUIRE(syscall_wait(child, nullptr, 0) == child);
        return 0;
    }

    Print("USABILITY_CHILD_READY\n");
    while (!usability_release)
    {
        ++usability_counter;
    }

    return 0;
}
