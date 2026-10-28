#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot/UEFI/include/elf.h"
#include "include/hackos/boot_elf.h"
#include "boot/BIOS/stage_2/src/fat32/boot_fat.h"

static FILE* disk;
static unsigned reads;
static unsigned failAt;

static int Read(uint32_t lba, void* output)
{
    assert(++reads < 100000);
    if (failAt && reads == failAt)
    {
        return 0;
    }
    return !fseek(disk, (long long)lba * 512, SEEK_SET) && fread(output, 1, 512, disk) == 512;
}

static void ElfChecks(void)
{
    Elf64_Ehdr h = {0};
    memcpy(h.e_ident, "\177ELF\2\1\1", 7);
    h.e_type = 2;
    h.e_machine = 62;
    h.e_version = 1;
    h.e_ehsize = sizeof(h);
    h.e_phentsize = sizeof(Elf64_Phdr);
    h.e_phoff = sizeof(h);
    h.e_phnum = 2;
    h.e_entry = 0x100000;
    Elf64_Phdr p[2] = {{1, 5, 4096, 0x100000, 0x100000, 4096, 4096, 4096},
                       {1, 6, 8192, 0x200000, 0x200000, 4096, 8192, 4096}};
    assert(HackosBootElfHeader(&h, 12288));
    assert(HackosBootElfSegments(&h, p, 12288));
    assert(!HackosBootElfHeader(&h, 63));
    assert(!HackosBootElfHeader(&h, 175));
    h.e_phoff = ~0ULL;
    assert(!HackosBootElfHeader(&h, 12288));
    h.e_phoff = 64;
    h.e_phnum = 129;
    assert(!HackosBootElfHeader(&h, 12288));
    h.e_phnum = 2;
    h.e_ident[6] = 0;
    assert(!HackosBootElfHeader(&h, 12288));
    h.e_ident[6] = 1;
    assert(!HackosBootElfSegments(&h, p, 12287));
    p[1].p_filesz = 8193;
    assert(!HackosBootElfSegments(&h, p, 20000));
    p[1].p_filesz = 4096;
    p[1].p_paddr = p[1].p_vaddr = 0x100000;
    assert(!HackosBootElfSegments(&h, p, 12288));
    p[1].p_paddr = p[1].p_vaddr = 0xfffff000;
    assert(!HackosBootElfSegments(&h, p, 12288));
    p[1].p_paddr = p[1].p_vaddr = 0x200000;
    p[1].p_align = 3;
    assert(!HackosBootElfSegments(&h, p, 12288));
    p[1].p_align = 4096;
    p[1].p_flags = 5;
    h.e_entry = 0x201000;
    assert(!HackosBootElfSegments(&h, p, 12288));
    h.e_entry = 0x100000;
    p[1].p_type = 3;
    assert(!HackosBootElfSegments(&h, p, 12288));
}

int main(int argc, char** argv)
{
    ElfChecks();
    if (argc == 1)
    {
        return 0;
    }
    assert(argc >= 3);
    disk = fopen(argv[1], "rb");
    assert(disk);
    failAt = argc > 3 ? atoi(argv[3]) : 0;
    int good = strcmp(argv[2], "-") != 0;
    assert(BootFatOpen(Read) == good);
    if (good)
    {
        FILE* expected = fopen(argv[2], "rb");
        assert(expected);
        assert(!fseek(expected, 0, SEEK_END));
        unsigned size = ftell(expected);
        assert(size == BootFatSize());
        unsigned char actual[997], wanted[997];
        for (unsigned offset = 0; offset < size; offset += sizeof(actual))
        {
            unsigned n = size - offset < sizeof(actual) ? size - offset : sizeof(actual);
            assert(BootFatRead(offset, actual, n));
            assert(!fseek(expected, offset, SEEK_SET));
            assert(fread(wanted, 1, n, expected) == n);
            assert(!memcmp(actual, wanted, n));
        }

        assert(BootFatRead(11, actual, sizeof(actual)));
        assert(!fseek(expected, 11, SEEK_SET));
        assert(fread(wanted, 1, sizeof(wanted), expected) == sizeof(wanted));
        assert(!memcmp(actual, wanted, sizeof(actual)));
        assert(!BootFatRead(size, actual, 1));
        assert(!BootFatRead(~0ULL, actual, 1));
        assert(BootFatRead(size, actual, 0));
        failAt = reads + 1;
        assert(!BootFatRead(0, actual, 1));
        fclose(expected);
    }

    fclose(disk);
    return 0;
}
