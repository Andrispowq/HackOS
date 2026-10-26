#include "elf.h"

#include "lib/memory.h"
#include "lib/stdio.h"

#include "arch/x86_64/paging/paging.h"
#include "drivers/ata/ata.h"

static uint8_t elf_check_file(Elf64_Ehdr* hdr)
{
    if(!hdr) 
        return 0;

	if((memcmp(&hdr->e_ident[EI_MAG0], (void*)ELFMAG, 4) != 0) ||
		hdr->e_ident[EI_CLASS] != ELFCLASS64 ||
		hdr->e_ident[EI_DATA] != ELFDATA2LSB ||
		hdr->e_ident[EI_VERSION] != EV_CURRENT ||
		hdr->e_type != ET_EXEC ||
		hdr->e_machine != EM_X86_64 ||
		hdr->e_version != EV_CURRENT)
	{
		return 0;
	}

	return 1;
}

// Validate every file-backed range before any segment is mapped or copied.
static bool elf_check_ranges(Elf64_Ehdr* header, uint64_t size)
{
    if (size < sizeof(Elf64_Ehdr) || !elf_check_file(header) ||
        header->e_ehsize != sizeof(Elf64_Ehdr) ||
        header->e_phentsize != sizeof(Elf64_Phdr) || !header->e_phnum ||
        header->e_phoff > size || header->e_phoff % alignof(Elf64_Phdr) ||
        header->e_phnum > (size - header->e_phoff) / sizeof(Elf64_Phdr)) return false;

    auto segments = (Elf64_Phdr*)((uint8_t*)header + header->e_phoff);
    bool entryFound = false;
    for (uint64_t i = 0; i < header->e_phnum; ++i)
    {
        const Elf64_Phdr& segment = segments[i];
        if (segment.p_type != PT_LOAD) continue;
        if (segment.p_filesz > segment.p_memsz || segment.p_offset > size ||
            segment.p_filesz > size - segment.p_offset || !segment.p_paddr ||
            segment.p_memsz > ~0ULL - segment.p_paddr ||
            segment.p_vaddr != segment.p_paddr) return false;
        if (segment.p_align > 1 &&
            ((segment.p_align & (segment.p_align - 1)) ||
             segment.p_vaddr % segment.p_align != segment.p_offset % segment.p_align)) return false;
        for (uint64_t j = 0; j < i; ++j)
            if (segments[j].p_type == PT_LOAD && segment.p_memsz && segments[j].p_memsz &&
                segment.p_paddr < segments[j].p_paddr + segments[j].p_memsz &&
                segments[j].p_paddr < segment.p_paddr + segment.p_memsz) return false;
        if ((segment.p_flags & 1) && header->e_entry >= segment.p_vaddr &&
            header->e_entry - segment.p_vaddr < segment.p_memsz) entryFound = true;
    }
    return entryFound;
}

#include "fs/filesystem.h"

Elf64_Ehdr* LoadProgram(const char* name, uint64_t* baseAddress)
{
    if (!baseAddress) return nullptr;
    *baseAddress = 0;
    Filesystem* drive = filesystems[0];
    if (!drive) return nullptr;
    ActiveFile* file = drive->OpenFile(name);
    if (!file) return nullptr;
    uint64_t size = file->GetSize();
    if (size < sizeof(Elf64_Ehdr)) { drive->CloseFile(file); return nullptr; }
    auto header = (Elf64_Ehdr*)kmalloc(size);
    if (!header) { drive->CloseFile(file); return nullptr; }
    int result = drive->Read(file, header, size);
    drive->CloseFile(file);
    if (result != 0 || !elf_check_ranges(header, size))
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
        if (segment.p_type != PT_LOAD || !segment.p_memsz) continue;
        uint64_t page = segment.p_paddr & ~0xFFFULL;
        uint64_t last = (segment.p_paddr + segment.p_memsz - 1) & ~0xFFFULL;
        for (;; page += 0x1000)
        {
            KernelDirectory.MapMemory(page, page);
            if (page == last) break;
        }
        memcpy((void*)segment.p_paddr, (void*)(baseAddress + segment.p_offset), segment.p_filesz);
        memset((void*)(segment.p_paddr + segment.p_filesz), 0, segment.p_memsz - segment.p_filesz);
    }
}

#include "proc/tasking/process.h"
#include "lib/data_structures/vector.h"
#include "arch/x86_64/timer/pit.h"

void program_task(const char* name)
{
	uint64_t addr;
    Elf64_Ehdr* hdr = LoadProgram(name, &addr);
    if(hdr == nullptr)
    {
        return;
    }

    PrepareProgram(hdr, addr);

	int argc = 2;
	char* argv[] = { (char*)name, "random_value" };

    int(*entry_point)(int, char*[]) = (int(*)(int, char*[]))hdr->e_entry;
    int res = entry_point(argc, argv);
    kprintf("Program (%s) returned with 0x%x!\n", name, res);
    _Kill();
}

void LoadProgramTask(const char* name)
{
	AddProcess(new Process(name, (void*)program_task, (void*)name));
}