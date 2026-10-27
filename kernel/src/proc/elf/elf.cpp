#include "arch/x86_64/interrupts/guard.h"
#include "elf.h"
#include "proc/user/address_space.h"

#include "lib/memory.h"
#include "lib/stdio.h"

#include "arch/x86_64/paging/paging.h"
#include "drivers/ata/ata.h"

static uint8_t elf_check_file(Elf64_Ehdr* hdr)
{
    if (!hdr)
    {
        return 0;
    }

    if ((memcmp(&hdr->e_ident[EI_MAG0], (void*)ELFMAG, 4) != 0) || hdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        hdr->e_ident[EI_DATA] != ELFDATA2LSB || hdr->e_ident[EI_VERSION] != EV_CURRENT || hdr->e_type != ET_EXEC ||
        hdr->e_machine != EM_X86_64 || hdr->e_version != EV_CURRENT)
    {
        return 0;
    }

    return 1;
}

// Validate every file-backed range before any segment is mapped or copied.
static bool elf_check_ranges(Elf64_Ehdr* header, uint64_t size, bool identity = true)
{
    if (size < sizeof(Elf64_Ehdr) || !elf_check_file(header) || header->e_ehsize != sizeof(Elf64_Ehdr) ||
        header->e_phentsize != sizeof(Elf64_Phdr) || !header->e_phnum || header->e_phoff > size ||
        header->e_phoff % alignof(Elf64_Phdr) || header->e_phnum > (size - header->e_phoff) / sizeof(Elf64_Phdr))
    {
        return false;
    }

    auto segments = (Elf64_Phdr*)((uint8_t*)header + header->e_phoff);
    bool entryFound = false;
    for (uint64_t i = 0; i < header->e_phnum; ++i)
    {
        const Elf64_Phdr& segment = segments[i];
        if (segment.p_type != PT_LOAD)
        {
            continue;
        }

        if (segment.p_filesz > segment.p_memsz || segment.p_offset > size || segment.p_filesz > size - segment.p_offset)
        {
            return false;
        }

        if (!segment.p_memsz)
        {
            continue; // Linkers may emit an empty PT_LOAD.
        }

        if (!segment.p_vaddr || segment.p_memsz > ~0ULL - segment.p_vaddr ||
            (identity && (segment.p_vaddr != segment.p_paddr || segment.p_vaddr >= USER_BASE)))
        {
            return false;
        }

        if (segment.p_align > 1 && ((segment.p_align & (segment.p_align - 1)) ||
                                    segment.p_vaddr % segment.p_align != segment.p_offset % segment.p_align))
        {
            return false;
        }

        for (uint64_t j = 0; j < i; ++j)
        {
            if (segments[j].p_type == PT_LOAD && segment.p_memsz && segments[j].p_memsz &&
                segment.p_vaddr < segments[j].p_vaddr + segments[j].p_memsz &&
                segments[j].p_vaddr < segment.p_vaddr + segment.p_memsz)
            {
                return false;
            }
        }

        if ((segment.p_flags & 1) && header->e_entry >= segment.p_vaddr &&
            header->e_entry - segment.p_vaddr < segment.p_memsz)
        {
            entryFound = true;
        }
    }

    return entryFound;
}

#include "fs/filesystem.h"

Elf64_Ehdr* LoadProgram(const char* name, uint64_t* baseAddress, bool identity)
{
    if (!baseAddress)
    {
        return nullptr;
    }

    *baseAddress = 0;
    Filesystem* drive = filesystems[0];
    if (!drive)
    {
        return nullptr;
    }

    ActiveFile* file = drive->OpenFile(name);
    if (!file)
    {
        return nullptr;
    }

    uint64_t size = file->GetSize();
    if (size < sizeof(Elf64_Ehdr) || (!identity && size > 1024 * 1024))
    {
        drive->CloseFile(file);
        return nullptr;
    }

    auto header = (Elf64_Ehdr*)kmalloc(size);
    if (!header)
    {
        drive->CloseFile(file);
        return nullptr;
    }

    int result = drive->Read(file, header, size);
    drive->CloseFile(file);
    if (result != 0 || !elf_check_ranges(header, size, identity))
    {
        kfree(header);
        return nullptr;
    }

    *baseAddress = (uint64_t)header;
    return header;
}

extern PageTableManager KernelDirectory;

void PrepareProgram(Elf64_Ehdr* header, uint64_t baseAddress)
{
    // LoadProgram has already validated these headers against the file size.
    auto segments = (Elf64_Phdr*)(baseAddress + header->e_phoff);
    for (uint64_t i = 0; i < header->e_phnum; ++i)
    {
        const Elf64_Phdr& segment = segments[i];
        if (segment.p_type != PT_LOAD || !segment.p_memsz)
        {
            continue;
        }

        uint64_t page = segment.p_paddr & ~0xFFFULL;
        uint64_t last = (segment.p_paddr + segment.p_memsz - 1) & ~0xFFFULL;
        for (;; page += 0x1000)
        {
            KernelDirectory.MapMemory(page, page);
            if (page == last)
            {
                break;
            }
        }

        memcpy((void*)segment.p_paddr, (void*)(baseAddress + segment.p_offset), segment.p_filesz);
        memset((void*)(segment.p_paddr + segment.p_filesz), 0, segment.p_memsz - segment.p_filesz);
    }
}

#include "proc/tasking/process.h"
#include "lib/data_structures/vector.h"
#include "arch/x86_64/timer/pit.h"

static uint64_t program_pid;

void program_task(void* parameter)
{
    const char* name = (const char*)parameter;
    int (*entry)(int, char*[]);
    {
        PreemptionGuard guard;
        uint64_t address;
        Elf64_Ehdr* header = LoadProgram(name, &address);
        if (!header)
        {
            kprintf("Cannot load program: %s\n", name);
            return;
        }

        PrepareProgram(header, address);
        entry = (int (*)(int, char*[]))header->e_entry;
        kfree(header);
    }

    char* argv[] = {(char*)name, nullptr};
    entry(1, argv);
    // TaskTrampoline handles programs that return normally.
}

void LoadProgramTask(const char* name)
{
    PreemptionGuard guard;
    // Fixed-address ELF programs cannot coexist in the shared address space.
    if (IsRunning(program_pid))
    {
        kprintf("A program is already running.\n");
        return;
    }

    auto process = new Process(name, (void*)program_task);
    process->parameter = process->name;
    program_pid = AddProcess(process);
}
