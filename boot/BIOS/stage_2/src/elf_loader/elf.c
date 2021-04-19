#include "elf.h"

#include "libc/memory.h"
#include "cpu/paging/paging.h"
#include "fat32/fat32.h"

static uint8_t elf_check_file(Elf64_Ehdr* hdr)
{
    if(!hdr) 
        return 0;

	if(memcmp(&hdr->e_ident[EI_MAG0], ELFMAG, 4) != 0 ||
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

Elf64_Ehdr* LoadProgram(const char* name, uint64_t* mem)
{
	uint64_t memory;
    DirectoryEntry entry;
    int ret = GetFile(name, &memory, &entry);
    if(ret != 0)
    {
        asm("hlt");
    }
	
	*mem = memory;

	Elf64_Ehdr* header = (Elf64_Ehdr*)memory;
	if(elf_check_file(header) != 0)
	{
		return header;
	}

	return 0;
}

void PrepareProgram(Elf64_Ehdr* header, uint64_t memory)
{
	Elf64_Phdr* phdrs = (Elf64_Phdr*)(memory + header->e_phoff);
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
                    MapMemory(i, i);
                }

                uint64_t off = memory + phdr->p_offset;
				uint64_t size = phdr->p_filesz;
                memcpy((void*)segment, (void*)off, size);
				break;
			}
		}
	}
}