#ifndef HACKOS_BOOT_ELF_H
#define HACKOS_BOOT_ELF_H

/* Include the platform's ELF64 declarations first. No hosted runtime required. */
#define HACKOS_BOOT_PHNUM 128
#define HACKOS_BOOT_ADDRESS_END 0x100000000ULL

static int HackosBootElfHeader(const Elf64_Ehdr* h, unsigned long long size)
{
    return size >= sizeof(*h) && h->e_ident[0] == 0x7f && h->e_ident[1] == 'E' && h->e_ident[2] == 'L' &&
           h->e_ident[3] == 'F' && h->e_ident[4] == 2 && h->e_ident[5] == 1 && h->e_ident[6] == 1 && h->e_type == 2 &&
           h->e_machine == 62 && h->e_version == 1 && h->e_ehsize == sizeof(*h) &&
           h->e_phentsize == sizeof(Elf64_Phdr) && h->e_phnum && h->e_phnum <= HACKOS_BOOT_PHNUM &&
           h->e_phoff <= size && h->e_phnum <= (size - h->e_phoff) / sizeof(Elf64_Phdr);
}

static int HackosBootElfSegments(const Elf64_Ehdr* h, const Elf64_Phdr* segments, unsigned long long size)
{
    int entryFound = 0;
    for (unsigned i = 0; i < h->e_phnum; ++i)
    {
        const Elf64_Phdr* p = segments + i;
        if (p->p_type == 2 || p->p_type == 3)
        {
            return 0;
        }
        if (p->p_type != PT_LOAD)
        {
            continue;
        }

        if (p->p_filesz > p->p_memsz || p->p_offset > size || p->p_filesz > size - p->p_offset ||
            p->p_vaddr != p->p_paddr || p->p_paddr < 0x100000 || p->p_paddr % 4096 ||
            p->p_paddr >= HACKOS_BOOT_ADDRESS_END || p->p_memsz > HACKOS_BOOT_ADDRESS_END - p->p_paddr ||
            (p->p_align > 1 &&
             ((p->p_align & (p->p_align - 1)) || p->p_vaddr % p->p_align != p->p_offset % p->p_align)))
        {
            return 0;
        }

        unsigned long long end = p->p_paddr + ((p->p_memsz + 4095) & ~4095ULL);
        for (unsigned j = 0; j < i; ++j)
        {
            const Elf64_Phdr* q = segments + j;
            unsigned long long otherEnd = q->p_paddr + ((q->p_memsz + 4095) & ~4095ULL);
            if (p->p_memsz && q->p_type == PT_LOAD && q->p_memsz && p->p_paddr < otherEnd && q->p_paddr < end)
            {
                return 0;
            }
        }

        if ((p->p_flags & 1) && h->e_entry >= p->p_vaddr && h->e_entry - p->p_vaddr < p->p_filesz)
        {
            entryFound = 1;
        }
    }

    return entryFound;
}

#endif
