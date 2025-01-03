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
		hdr->e_type != ET_EXEC ||
		hdr->e_machine != EM_X86_64 ||
		hdr->e_version != EV_CURRENT)
	{
		return 0;
	}

	return 1;
}

#include "fs/filesystem.h"

Elf64_Ehdr* LoadProgram(const char* name, uint64_t* baseAddress)
{
	Filesystem* drive_C = filesystems[0];
	if(drive_C == nullptr)
	{
		return nullptr;
	}

	ActiveFile* file = drive_C->OpenFile(name); //Let's assume for now that we have a filesystem
	if(file == nullptr) 
	{
		kprintf("Failed to open file %s!\n", name);
		return nullptr;
	}

	uint64_t size = file->GetSize();
	uint64_t memory = kmalloc(size);
	drive_C->Read(file, (void*)memory, size);
	drive_C->CloseFile(file);

	*baseAddress = memory;

	Elf64_Ehdr* header = (Elf64_Ehdr*)memory;
	if(elf_check_file(header) == 0)
	{
		kfree((void*)memory);
		return nullptr;
	}

	return header;
}

extern PageTableManager KernelDirectory;
void PrepareProgram(Elf64_Ehdr* header, uint64_t baseAddress)
{
	Elf64_Phdr* phdrs = (Elf64_Phdr*)(baseAddress + header->e_phoff);
	for (
		Elf64_Phdr* phdr = phdrs;
		(uint64_t)phdr < (uint64_t)phdrs + header->e_phnum * header->e_phentsize;
		phdr = (Elf64_Phdr*)((uint64_t)phdr + header->e_phentsize)
	)
	{
		switch (phdr->p_type)
		{
			case PT_LOAD:
			{
				uint32_t pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
				Elf64_Addr segment = phdr->p_paddr;
				
                for(uint64_t i = segment; i < segment + pages * 0x1000; i += 0x1000)
                {
                    KernelDirectory.MapMemory(i, i);
                }

                uint64_t off = baseAddress + phdr->p_offset;
				uint64_t size = phdr->p_filesz;
				memset((void*)segment, 0, size);
                memcpy((void*)segment, (void*)off, size);
				break;
			}
		}
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