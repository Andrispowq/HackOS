// Compile with the userspace symbol renames, separately from hosted headers.
// Only syscall transport is replaced; command parsing/matching/I/O loops and
// libc's User::Write and string operations are the production sources.
#include "hackos/user.h"

extern "C" uint64_t host_open(const char*, uint64_t);
extern "C" uint64_t host_read(uint64_t, void*, uint64_t);
extern "C" uint64_t host_write(uint64_t, const void*, uint64_t);
extern "C" uint64_t host_close(uint64_t);
extern "C" uint64_t host_terminal_info(uint64_t, void*);
extern "C" uint64_t host_terminal_read();

uint64_t syscall_fd_open(const char* path, uint64_t mode)
{
    return host_open(path, mode);
}

uint64_t syscall_fd_read(uint64_t fd, void* data, uint64_t length, uint64_t)
{
    return host_read(fd, data, length);
}

uint64_t syscall_fd_write(uint64_t fd, const void* data, uint64_t length, uint64_t)
{
    return host_write(fd, data, length);
}

uint64_t syscall_fd_close(uint64_t fd)
{
    return host_close(fd);
}

uint64_t syscall_terminal_info(uint64_t fd, HackosTerminalInfo* info)
{
    return host_terminal_info(fd, info);
}

uint64_t syscall_terminal_read(uint64_t)
{
    return host_terminal_read();
}
