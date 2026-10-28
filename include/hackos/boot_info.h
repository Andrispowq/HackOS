#ifndef HACKOS_BOOT_INFO_H
#define HACKOS_BOOT_INFO_H

// x86-64 SysV loader/kernel ABI. Compiler integer types avoid hosted headers
// and GNU-EFI's independent typedefs. Keep this record naturally aligned.
#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct psf1_hdr
{
    unsigned char magic[2];
    unsigned char mode;
    unsigned char charsize;
} PSF1_HEADER;

typedef struct psf1_font
{
    PSF1_HEADER* psf1_Header;
    void* glyphBuffer;
} PSF1_FONT;

typedef struct BootFramebuffer
{
    __UINT32_TYPE__* address;
    // Pitch is in 32-bit pixels, not bytes.
    __UINT32_TYPE__ width, height, bpp, pitch;
} BootFramebuffer;

typedef struct MemoryMapEntry
{
    __UINT64_TYPE__ address;
    __UINT64_TYPE__ size;
    // Firmware-native type numbers: E820 for BIOS, EFI for UEFI.
    __UINT32_TYPE__ type;
    __UINT32_TYPE__ attributes;
} MemoryMapEntry;

typedef struct KernelInfo
{
    BootFramebuffer framebuffer;
    PSF1_FONT* font;
    MemoryMapEntry* memMap;
    __UINT64_TYPE__ memMapEntrySize;
    void* rsdp;
    __UINT8_TYPE__ memMapEntries;
    __UINT8_TYPE__ rsdp_version;
    __UINT8_TYPE__ booted_from_BIOS;
} KernelInfo;

#ifdef __cplusplus
#define HACKOS_BOOT_ASSERT(condition) static_assert(condition, #condition)
#define HACKOS_BOOT_ALIGN(type) alignof(type)
#else
#define HACKOS_BOOT_ASSERT(condition) _Static_assert(condition, #condition)
#define HACKOS_BOOT_ALIGN(type) _Alignof(type)
#endif
#define HACKOS_BOOT_OFFSET(type, field, offset) HACKOS_BOOT_ASSERT(__builtin_offsetof(type, field) == offset)

HACKOS_BOOT_ASSERT(sizeof(void*) == 8);

HACKOS_BOOT_ASSERT(sizeof(PSF1_HEADER) == 4);
HACKOS_BOOT_ASSERT(HACKOS_BOOT_ALIGN(PSF1_HEADER) == 1);
HACKOS_BOOT_OFFSET(PSF1_HEADER, magic, 0);
HACKOS_BOOT_OFFSET(PSF1_HEADER, mode, 2);
HACKOS_BOOT_OFFSET(PSF1_HEADER, charsize, 3);

HACKOS_BOOT_ASSERT(sizeof(PSF1_FONT) == 16);
HACKOS_BOOT_ASSERT(HACKOS_BOOT_ALIGN(PSF1_FONT) == 8);
HACKOS_BOOT_OFFSET(PSF1_FONT, psf1_Header, 0);
HACKOS_BOOT_OFFSET(PSF1_FONT, glyphBuffer, 8);

HACKOS_BOOT_ASSERT(sizeof(BootFramebuffer) == 24);
HACKOS_BOOT_ASSERT(HACKOS_BOOT_ALIGN(BootFramebuffer) == 8);
HACKOS_BOOT_OFFSET(BootFramebuffer, address, 0);
HACKOS_BOOT_OFFSET(BootFramebuffer, width, 8);
HACKOS_BOOT_OFFSET(BootFramebuffer, height, 12);
HACKOS_BOOT_OFFSET(BootFramebuffer, bpp, 16);
HACKOS_BOOT_OFFSET(BootFramebuffer, pitch, 20);

HACKOS_BOOT_ASSERT(sizeof(MemoryMapEntry) == 24);
HACKOS_BOOT_ASSERT(HACKOS_BOOT_ALIGN(MemoryMapEntry) == 8);
HACKOS_BOOT_OFFSET(MemoryMapEntry, address, 0);
HACKOS_BOOT_OFFSET(MemoryMapEntry, size, 8);
HACKOS_BOOT_OFFSET(MemoryMapEntry, type, 16);
HACKOS_BOOT_OFFSET(MemoryMapEntry, attributes, 20);

HACKOS_BOOT_ASSERT(sizeof(KernelInfo) == 64);
HACKOS_BOOT_ASSERT(HACKOS_BOOT_ALIGN(KernelInfo) == 8);
HACKOS_BOOT_OFFSET(KernelInfo, framebuffer, 0);
HACKOS_BOOT_OFFSET(KernelInfo, font, 24);
HACKOS_BOOT_OFFSET(KernelInfo, memMap, 32);
HACKOS_BOOT_OFFSET(KernelInfo, memMapEntrySize, 40);
HACKOS_BOOT_OFFSET(KernelInfo, rsdp, 48);
HACKOS_BOOT_OFFSET(KernelInfo, memMapEntries, 56);
HACKOS_BOOT_OFFSET(KernelInfo, rsdp_version, 57);
HACKOS_BOOT_OFFSET(KernelInfo, booted_from_BIOS, 58);

#undef HACKOS_BOOT_OFFSET
#undef HACKOS_BOOT_ALIGN
#undef HACKOS_BOOT_ASSERT

#endif
