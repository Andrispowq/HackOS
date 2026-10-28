#include "hackos/user.h"
#include "hackos/stdio.h"
using namespace User;

static int Fail(unsigned line)
{
    printf("STREAM_PROBE_FAIL %u\n", line);
    return 1;
}

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            return Fail(__LINE__);                                                                                     \
        }                                                                                                              \
    } while (0)

static uint64_t Spawn(const char* mode)
{
    const char* args[] = {"~/USR/BIN/STREAMPROBE", mode};
    return syscall_spawn_args(args[0], 2, args);
}

static bool Reaped(uint64_t pid)
{
    // Child completion and idle reclamation are independent. Inherited file
    // references remain live until reaping, unlike pipe endpoints at exit.
    for (unsigned attempt = 0; attempt < 2000; ++attempt)
    {
        bool found = false;
        HackosProcessInfo info;
        for (uint64_t index = 0;; ++index)
        {
            uint64_t result = syscall_process_info(index, &info);
            if (result == HACKOS_ERROR)
            {
                return false;
            }
            if (!result)
            {
                break;
            }
            if (info.pid == pid)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return true;
        }
        if (syscall_sleep(1) != 0)
        {
            return false;
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    static char data[1024];
    if (argc == 2 && Equal(argv[1], "source"))
    {
        for (unsigned block = 0; block < 12; ++block)
        {
            for (unsigned i = 0; i < sizeof(data); ++i)
            {
                data[i] = (char)((block * sizeof(data) + i) % 251);
            }

            CHECK(Write(1, data, sizeof(data)));
        }

        return 0;
    }

    if (argc == 2 && Equal(argv[1], "both"))
    {
        CHECK(printf("STREAM_%s\n", "STDOUT") == 14);
        Error("STREAM_STDERR\n");
        return 0;
    }

    if (argc == 2 && Equal(argv[1], "writer"))
    {
        CHECK(syscall_fd_close(3) == 0);
        CHECK(syscall_sleep(150) == 0);
        CHECK(Write(4, "child", 5));
        return 23; // Exit releases the last writer even before idle reaps us.
    }

    if (argc == 2 && Equal(argv[1], "reader"))
    {
        CHECK(syscall_fd_close(4) == 0);
        CHECK(syscall_sleep(150) == 0);
        uint64_t total = 0;
        while (total < HACKOS_PIPE_CAPACITY + 1)
        {
            uint64_t count = syscall_fd_read(3, data, sizeof(data), HACKOS_WAIT_FOREVER);
            CHECK(count && count <= sizeof(data));
            total += count;
            if (total == HACKOS_PIPE_CAPACITY + 1)
            {
                CHECK(data[count - 1] == 'x');
            }
        }

        CHECK(total == HACKOS_PIPE_CAPACITY + 1);
        return 0;
    }

    if (argc == 2 && Equal(argv[1], "hang"))
    {
        Print("STREAM_BLOCK_READY\n");
        CHECK(syscall_poll(nullptr, 0, HACKOS_WAIT_FOREVER) == 0);
        return 1;
    }

    if (argc == 2 && Equal(argv[1], "fault"))
    {
        *(volatile int*)0x12345 = 1;
        return 1;
    }

    if (argc == 2 && Equal(argv[1], "inherit"))
    {
        CHECK(Write(10, "B", 1));
        return 0;
    }

    if (argc == 2 && Equal(argv[1], "held"))
    {
        CHECK(syscall_fd_read(3, data, 1, 0) == HACKOS_ERROR);
        CHECK(syscall_sleep(5000) == 0);
        return 0;
    }

    CHECK(argc == 1);
    uint64_t ends[2] = {};
    CHECK(syscall_pipe((uint64_t*)"readonly") == HACKOS_ERROR);
    CHECK(syscall_pipe((uint64_t*)0x12345) == HACKOS_ERROR);
    CHECK(syscall_pipe(ends) == 0 && ends[0] == 3 && ends[1] == 4);
    CHECK(syscall_fd_read(3, data, 1, 0) == HACKOS_WOULD_BLOCK);
    CHECK(syscall_fd_read(3, data, 1, 20) == HACKOS_TIMED_OUT);
    CHECK(syscall_fd_write(3, data, 1, 0) == HACKOS_DENIED);
    CHECK(syscall_fd_read(4, data, 1, 0) == HACKOS_DENIED);
    CHECK(syscall_fd_write(4, (void*)0x12345, 1, 0) == HACKOS_ERROR);
    CHECK(syscall_fd_write(4, data, 1025, 0) == HACKOS_ERROR);
    CHECK(syscall_fd_read(3, data, 1, 0x100000000ULL) == HACKOS_ERROR);
    CHECK(syscall_fd_seek(3, 0) == HACKOS_ERROR);
    CHECK(syscall_fd_flush(4) == HACKOS_ERROR);
    CHECK(syscall_fd_close(77) == HACKOS_ERROR);
    CHECK(syscall_dup2(99, 3) == HACKOS_ERROR);
    CHECK(Write(4, "abc", 3));
    CHECK(syscall_fd_read(3, (void*)"readonly", 3, 0) == HACKOS_ERROR);
    CHECK(syscall_poll((HackosPollFd*)"readonly", 1, 0) == HACKOS_ERROR);
    HackosPollFd poll[3] = {{3, HACKOS_POLL_READ, 0}, {4, HACKOS_POLL_WRITE, 0}, {88, 0, 0}};
    CHECK(syscall_poll(poll, 3, 0) == 3);
    CHECK(poll[0].revents == HACKOS_POLL_READ && poll[1].revents == HACKOS_POLL_WRITE &&
          poll[2].revents == HACKOS_POLL_INVALID);
    CHECK(syscall_fd_read(3, data, 3, 0) == 3 && data[0] == 'a' && data[2] == 'c');
    CHECK(syscall_dup2(4, 5) == 5 && syscall_dup2(5, 5) == 5);
    CHECK(syscall_fd_close(4) == 0);
    CHECK(syscall_fd_read(3, data, 1, 0) == HACKOS_WOULD_BLOCK);
    CHECK(syscall_fd_close(5) == 0);
    CHECK(syscall_poll(poll, 1, 0) == 1 && poll[0].revents == HACKOS_POLL_HANGUP);
    CHECK(syscall_fd_read(3, data, 1, 0) == 0);
    CHECK(syscall_fd_close(3) == 0);

    CHECK(syscall_pipe(ends) == 0);
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK(Write(4, data, sizeof(data)));
    }

    CHECK(syscall_fd_write(4, "x", 1, 0) == HACKOS_WOULD_BLOCK);
    CHECK(syscall_fd_write(4, "x", 1, 20) == HACKOS_TIMED_OUT);
    poll[0] = {4, HACKOS_POLL_WRITE, 0};
    CHECK(syscall_poll(poll, 1, 20) == 0 && poll[0].revents == 0);
    uint64_t child = Spawn("reader");
    CHECK(child != HACKOS_ERROR);
    CHECK(syscall_fd_close(3) == 0);
    CHECK(syscall_fd_write(4, "x", 1, 2000) == 1);
    int64_t status = 0;
    CHECK(syscall_wait_timeout(child, &status, 0, 2000) == child && status == 0);
    CHECK(syscall_fd_write(4, "x", 1, 0) == HACKOS_BROKEN_PIPE);
    CHECK(syscall_poll(poll, 1, 0) == 1 && poll[0].revents == HACKOS_POLL_ERROR);
    CHECK(syscall_fd_close(4) == 0);

    CHECK(syscall_pipe(ends) == 0);
    child = Spawn("writer");
    CHECK(child != HACKOS_ERROR);
    CHECK(syscall_fd_close(4) == 0);
    CHECK(syscall_wait_timeout(child, (int64_t*)"readonly", 0, 0) == HACKOS_ERROR);
    CHECK(syscall_wait_timeout(child, &status, 0, 10) == HACKOS_TIMED_OUT);
    poll[0] = {3, HACKOS_POLL_READ, 0};
    CHECK(syscall_poll(poll, 1, 2000) == 1);
    CHECK(syscall_fd_read(3, data, sizeof(data), 2000) == 5 && data[0] == 'c');
    CHECK(syscall_fd_read(3, data, sizeof(data), 2000) == 0);
    CHECK(syscall_wait_timeout(child, &status, 0, 2000) == child && status == 23);
    CHECK(syscall_wait_timeout(child, nullptr, 0, 0) == HACKOS_ERROR);
    CHECK(syscall_fd_close(3) == 0);
    CHECK(syscall_sleep(0) == 0 && syscall_sleep(HACKOS_WAIT_FOREVER) == HACKOS_ERROR);
    CHECK(syscall_poll(nullptr, 0, 10) == 0);

    // Every local slot can be duplicated; exhaustion must not allocate a pipe.
    for (unsigned i = 3; i < HACKOS_FD_LIMIT; ++i)
    {
        CHECK(syscall_dup2(1, i) == i);
    }

    CHECK(syscall_pipe(ends) == HACKOS_ERROR);
    for (unsigned i = 3; i < HACKOS_FD_LIMIT; ++i)
    {
        CHECK(syscall_fd_close(i) == 0);
    }

    // Held jobs transfer only the selected standard streams. Timeouts and bad
    // destinations leave the whole-job result available to the controller.
    HackosSpawnIo specification = {{0, 1, 2}, 0};
    const char* args[] = {"~/USR/BIN/STREAMPROBE", "held"};
    CHECK(syscall_spawn_io(args[0], 2, args, (const HackosSpawnIo*)0x12345) == HACKOS_ERROR);
    CHECK(syscall_pipe(ends) == 0);
    uint64_t group = syscall_spawn_io(args[0], 2, args, &specification);
    CHECK(group != HACKOS_ERROR && group != HACKOS_DENIED);
    CHECK(syscall_job_control(group, HACKOS_JOB_BACKGROUND) == 0);
    CHECK(syscall_job_wait_timeout(group, (int64_t*)"readonly", 0, 0) == HACKOS_ERROR);
    CHECK(syscall_job_wait_timeout(group, &status, 0, 20) == HACKOS_TIMED_OUT);
    CHECK(syscall_job_wait_timeout(group, &status, HACKOS_WAIT_NOHANG, 0) == 0);
    CHECK(syscall_job_control(group, HACKOS_JOB_TERMINATE) == 0);
    CHECK(syscall_job_wait_timeout(group, &status, 0, 2000) == group && status == 137);
    CHECK(syscall_job_wait_timeout(group, nullptr, 0, 0) == HACKOS_ERROR);
    CHECK(syscall_fd_close(3) == 0 && syscall_fd_close(4) == 0);
    for (unsigned i = 0; i < 64; ++i)
    {
        CHECK(syscall_pipe(ends) == 0);
        CHECK(syscall_fd_close(ends[0]) == 0 && syscall_fd_close(ends[1]) == 0);
    }

    uint64_t file = syscall_fd_open("/tmp/streams.dat", HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_TRUNCATE);
    CHECK(file == 3 && Write(file, "A", 1));
    CHECK(syscall_open("/tmp/streams.dat") == HACKOS_ERROR);
    CHECK(syscall_file_save("/tmp/streams.dat", "x", 1, 0) == HACKOS_ERROR);
    CHECK(syscall_dup2(file, 10) == 10);
    child = Spawn("inherit");
    CHECK(syscall_wait_timeout(child, &status, 0, 2000) == child && status == 0);
    CHECK(Reaped(child));
    CHECK(Write(file, "C", 1));
    CHECK(syscall_fd_close(10) == 0 && syscall_fd_flush(file) == 0 && syscall_fd_close(file) == 0);
    file = syscall_fd_open("/tmp/streams.dat", HACKOS_OPEN_READ);
    CHECK(file == 3 && syscall_fd_read(file, data, sizeof(data), 0) == 3);
    CHECK(data[0] == 'A' && data[1] == 'B' && data[2] == 'C');
    CHECK(syscall_fd_seek(file, 0) == 0);
    CHECK(syscall_dup2(file, 5) == 5);
    CHECK(syscall_fd_read(5, data, 1, 0) == 1 && data[0] == 'A');
    CHECK(syscall_fd_read(file, data, 1, 0) == 1 && data[0] == 'B');
    CHECK(syscall_fd_close(5) == 0 && syscall_fd_close(file) == 0);
    uint64_t legacy[8];
    for (unsigned i = 0; i < 8; ++i)
    {
        legacy[i] = syscall_open("/tmp/streams.dat");
        CHECK(legacy[i] != HACKOS_ERROR);
    }

    CHECK(syscall_open("/tmp/streams.dat") == HACKOS_ERROR);
    file = syscall_fd_open("/tmp/streams.dat", HACKOS_OPEN_READ);
    CHECK(file == 3 && syscall_fd_read(file, data, 3, 0) == 3);
    CHECK(syscall_fd_close(file) == 0);
    for (auto handle : legacy)
    {
        CHECK(syscall_close(handle) == 0);
    }

    Print("STREAM_RUNTIME_OK\n");
    return 0;
}
