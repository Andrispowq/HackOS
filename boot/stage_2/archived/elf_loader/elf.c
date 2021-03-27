#include "elf.h"

uint8_t elf_check_file(Elf32_Ehdr* hdr)
{
    if(!hdr) 
        return 0;
    
	if(hdr->e_ident[EI_MAG0] != ELFMAG0) 
    {
		printf("ELF Header EI_MAG0 incorrect.\n");
		return 0;
	}

	if(hdr->e_ident[EI_MAG1] != ELFMAG1)
    {
		printf("ELF Header EI_MAG1 incorrect.\n");
		return 0;
	}

	if(hdr->e_ident[EI_MAG2] != ELFMAG2) 
    {
		printf("ELF Header EI_MAG2 incorrect.\n");
		return 0;
	}

	if(hdr->e_ident[EI_MAG3] != ELFMAG3) 
    {
		printf("ELF Header EI_MAG3 incorrect.\n");
		return 0;
	}

	return 1;
}

uint8_t elf_check_supported(Elf32_Ehdr *hdr) 
{
	if(!elf_check_file(hdr)) 
    {
		printf("Invalid ELF File.\n");
		return 0;
	}

	if(hdr->e_ident[EI_CLASS] != ELFCLASS32) 
    {
		printf("Unsupported ELF File Class.\n");
		return 0;
	}

	if(hdr->e_ident[EI_DATA] != ELFDATA2LSB) 
    {
		printf("Unsupported ELF File byte order.\n");
		return 0;
	}

	if(hdr->e_machine != EM_386) 
    {
		printf("Unsupported ELF File target.\n");
		return 0;
	}

	if(hdr->e_ident[EI_VERSION] != EV_CURRENT) 
    {
		printf("Unsupported ELF File version.\n");
		return 0;
	}

	if(hdr->e_type != ET_REL && hdr->e_type != ET_EXEC) 
    {
		printf("Unsupported ELF File type.\n");
		return 0;
	}

	return 1;
}