#pragma once
#include "command.h"

// Buffers are reserved in each program's private BSS; there is no user heap.
// A successful load closes its owned read handle before a later save/move.
inline uint64_t LoadWholeFile(const char* path, char* data, uint64_t& size)
{
    HackosFileInfo info;
    uint64_t status = syscall_stat(path, &info);
    if (status)
    {
        return status;
    }

    if ((info.attributes & HACKOS_DIRECTORY) || info.size > HACKOS_FILE_LIMIT)
    {
        Error("Expected a file of at most 32768 bytes.\n");
        return HACKOS_ERROR;
    }

    uint64_t handle = syscall_open(path);
    if (handle == HACKOS_ERROR)
    {
        return HACKOS_ERROR;
    }

    size = 0;
    while (size < HACKOS_FILE_LIMIT)
    {
        uint64_t count = HACKOS_FILE_LIMIT - size;
        if (count > 512)
        {
            count = 512;
        }

        uint64_t read = syscall_read(handle, data + size, count);
        if (read == HACKOS_ERROR)
        {
            syscall_close(handle);
            return HACKOS_ERROR;
        }

        if (!read)
        {
            break;
        }

        size += read;
    }

    char extra;
    status = syscall_read(handle, &extra, 1);
    syscall_close(handle);
    return status == 0 ? 0 : HACKOS_ERROR;
}
