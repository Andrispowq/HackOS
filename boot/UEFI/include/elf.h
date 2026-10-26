#ifndef HACKOS_UEFI_ELF_H
#define HACKOS_UEFI_ELF_H
/* ELF64 definitions used by the x86-64 loader and GNU-EFI relocator.
 * Kept here so the bare-metal toolchain does not need a host libc sysroot. */
#include <stdint.h>
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef struct {
    unsigned char e_ident[16];
    Elf64_Half e_type, e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff, e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;
typedef struct {
    Elf64_Word p_type, p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr, p_paddr;
    Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;
typedef struct {
    Elf64_Sxword d_tag;
    union { Elf64_Xword d_val; Elf64_Addr d_ptr; } d_un;
} Elf64_Dyn;
typedef struct { Elf64_Addr r_offset; Elf64_Xword r_info; } Elf64_Rel;
#define EI_MAG0 0
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_X86_64 62
#define EV_CURRENT 1
#define PT_LOAD 1
#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define ELF64_R_TYPE(info) ((uint32_t)(info))
#define R_X86_64_NONE 0
#define R_X86_64_RELATIVE 8
#endif
