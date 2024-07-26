#include "filesystem.h"
#include "lib/memory.h"
#include "lib/stdio.h"

Filesystem* filesystems[MAX_FILESYSTEMS];

void InitialiseFilesystems()
{
    memset(filesystems, 0, sizeof(Filesystem*) * MAX_FILESYSTEMS);
}

void RegisterFilesystem(Filesystem* filesystem)
{
    for(uint32_t i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if(filesystems[i] == nullptr)
        {
            filesystems[i] = filesystem;
            return;
        }
    }

    kprintf("KERNEL ERROR: Too many filesystems added!\n");
    asm("hlt");
}

void UnregisterFilesystem(Filesystem* filesystem)
{
    for(uint32_t i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if(filesystems[i] == filesystem)
        {
            filesystems[i] = nullptr;
            return;
        }
    }

    kprintf("KERNEL ERROR: Can't find the filesystem!\n");
    asm("hlt");
}