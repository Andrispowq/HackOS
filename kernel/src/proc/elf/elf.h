#ifndef ELF_H
#define ELF_H

#include "lib/stdint.h"
 
typedef uint16_t Elf64_Half;
typedef uint64_t Elf64_Off;
typedef uint64_t Elf64_Addr;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_XWord;
typedef int32_t  Elf64_Sword;

#define ELF_NIDENT	16
 
typedef struct 
{
	uint8_t		e_ident[ELF_NIDENT];
	Elf64_Half	e_type;
	Elf64_Half	e_machine;
	Elf64_Word	e_version;
	Elf64_Addr	e_entry;
	Elf64_Off	e_phoff;
	Elf64_Off	e_shoff;
	Elf64_Word	e_flags;
	Elf64_Half	e_ehsize;
	Elf64_Half	e_phentsize;
	Elf64_Half	e_phnum;
	Elf64_Half	e_shentsize;
	Elf64_Half	e_shnum;
	Elf64_Half	e_shstrndx;
} Elf64_Ehdr;

typedef struct 
{
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_XWord p_offset;
    Elf64_XWord p_vaddr;
    Elf64_XWord p_paddr;
    Elf64_XWord p_filesz;
    Elf64_XWord p_memsz;
    Elf64_XWord p_align;
} Elf64_Phdr;

enum Elf_Ident 
{
	EI_MAG0		= 0, // 0x7F
	EI_MAG1		= 1, // 'E'
	EI_MAG2		= 2, // 'L'
	EI_MAG3		= 3, // 'F'
	EI_CLASS	= 4, // Architecture (32/64)
	EI_DATA		= 5, // Byte Order
	EI_VERSION	= 6, // ELF Version
	EI_OSABI	= 7, // OS Specific
	EI_ABIVERSION	= 8, // OS Specific
	EI_PAD		= 9  // Padding
};

enum Phdr_Type
{
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_TLS = 7
};
 
#define ELFMAG	"\177ELF"
#define ELFDATA2LSB	1  // Little Endian

#define ELFCLASS32	1  // 32-bit Architecture
#define ELFCLASS64	2  // 64-bit Architecture

enum Elf_Type 
{
	ET_NONE		= 0, // Unkown Type
	ET_REL		= 1, // Relocatable File
	ET_EXEC		= 2  // Executable File
};
 
#define EM_386		0x03  // x86 Machine Type
#define EM_X86_64   0x3E  // x86-64 Machine Type

#define EV_CURRENT	0x01  // ELF Current Version

#define ET_EXEC 	0x02  // ELF is executable

Elf64_Ehdr* LoadProgram(const char* name, uint64_t* baseAddress);
void PrepareProgram(Elf64_Ehdr* header, uint64_t baseAddress);

void LoadProgramTask(const char* name);

#endif