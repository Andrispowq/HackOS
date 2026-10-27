#include "../../../../include/hackos/serial.h"

#include <efi.h>
#include <efilib.h>
#include <elf.h>

static int serialAvailable;

static void SerialLog(const char* text)
{
    while (serialAvailable && *text)
    {
        serialAvailable = HackosSerialPut(*text++);
    }
}

typedef unsigned long long size_t;

typedef struct
{
    unsigned int* address;
    unsigned int width, height, bpp, pitch;
} Framebuffer;

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct
{
    unsigned char magic[2];
    unsigned char mode;
    unsigned char charsize;
} PSF1_HEADER;

typedef struct
{
    PSF1_HEADER* psf1_Header;
    void* glyphBuffer;
} PSF1_FONT;

typedef struct memory_map_entry
{
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t extended_attributes;
} MemoryMapEntry;

typedef struct _bootinfo
{
    Framebuffer framebuffer;
    PSF1_FONT* font;
    MemoryMapEntry* memMap;
    uint64_t memMapEntrySize;
    void* rsdp;
    uint8_t memMapEntries;
    uint8_t rsdp_revision;
    uint8_t booted_from_BIOS;
} BOOTINFO;

static int EqualBytes(const void* a, const void* b, UINTN size)
{
    const UINT8* left = a;
    const UINT8* right = b;
    for (UINTN i = 0; i < size; ++i)
    {
        if (left[i] != right[i])
        {
            return 0;
        }
    }

    return 1;
}

static EFI_STATUS ReadAt(EFI_FILE* file, UINT64 offset, void* buffer, UINTN size)
{
    EFI_STATUS status = file->SetPosition(file, offset);
    if (EFI_ERROR(status))
    {
        return status;
    }

    UINTN actual = size;
    status = file->Read(file, &actual, buffer);
    return EFI_ERROR(status) ? status : actual == size ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

static EFI_FILE* LoadFile(CHAR16* path, EFI_HANDLE image)
{
    EFI_LOADED_IMAGE_PROTOCOL* loaded;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem;
    EFI_FILE* directory;
    EFI_FILE* file = NULL;
    if (EFI_ERROR(BS->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (void**)&loaded)) ||
        EFI_ERROR(BS->HandleProtocol(loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&filesystem)) ||
        EFI_ERROR(filesystem->OpenVolume(filesystem, &directory)))
    {
        return NULL;
    }

    EFI_STATUS status = directory->Open(directory, &file, path, EFI_FILE_MODE_READ, 0);
    directory->Close(directory);
    return EFI_ERROR(status) ? NULL : file;
}

static PSF1_FONT* LoadFont(EFI_HANDLE image)
{
    EFI_FILE* file = LoadFile(L"\\UTIL\\FONTS\\zap-light16.psf", image);
    if (!file)
    {
        return NULL;
    }

    PSF1_FONT* font = AllocateZeroPool(sizeof(*font));
    if (!font)
    {
        file->Close(file);
        return NULL;
    }

    font->psf1_Header = AllocatePool(sizeof(PSF1_HEADER));
    if (!font->psf1_Header)
    {
        goto failed;
    }

    if (EFI_ERROR(ReadAt(file, 0, font->psf1_Header, sizeof(PSF1_HEADER))) ||
        font->psf1_Header->magic[0] != PSF1_MAGIC0 || font->psf1_Header->magic[1] != PSF1_MAGIC1 ||
        font->psf1_Header->charsize != 16)
    {
        goto failed;
    }

    UINTN size = font->psf1_Header->charsize * ((font->psf1_Header->mode & 1) ? 512 : 256);
    font->glyphBuffer = AllocatePool(size);
    if (!font->glyphBuffer || EFI_ERROR(ReadAt(file, sizeof(PSF1_HEADER), font->glyphBuffer, size)))
    {
        goto failed;
    }

    file->Close(file);
    return font;
failed:
    if (font->glyphBuffer)
    {
        FreePool(font->glyphBuffer);
    }

    if (font->psf1_Header)
    {
        FreePool(font->psf1_Header);
    }

    FreePool(font);
    file->Close(file);
    return NULL;
}

static EFI_STATUS LoadKernel(EFI_FILE* file, UINT64* entry)
{
    EFI_FILE_INFO* info = LibFileInfo(file);
    if (!info)
    {
        return EFI_LOAD_ERROR;
    }

    UINT64 fileSize = info->FileSize;
    FreePool(info);
    Elf64_Ehdr header;
    if (EFI_ERROR(ReadAt(file, 0, &header, sizeof header)) || !EqualBytes(header.e_ident, ELFMAG, SELFMAG) ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT || header.e_type != ET_EXEC || header.e_machine != EM_X86_64 ||
        header.e_version != EV_CURRENT || header.e_ehsize != sizeof header ||
        header.e_phentsize != sizeof(Elf64_Phdr) || !header.e_phnum || header.e_phoff > fileSize ||
        header.e_phnum > (fileSize - header.e_phoff) / sizeof(Elf64_Phdr))
    {
        return EFI_LOAD_ERROR;
    }

    UINTN bytes = header.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr* segments = AllocatePool(bytes);
    if (!segments)
    {
        return EFI_OUT_OF_RESOURCES;
    }

    EFI_STATUS status = ReadAt(file, header.e_phoff, segments, bytes);
    int entryFound = 0;
    if (EFI_ERROR(status))
    {
        FreePool(segments);
        return status;
    }

    for (UINTN i = 0; i < header.e_phnum; ++i)
    {
        Elf64_Phdr* p = &segments[i];
        if (p->p_type != PT_LOAD)
        {
            continue;
        }

        if (p->p_filesz > p->p_memsz || p->p_offset > fileSize || p->p_filesz > fileSize - p->p_offset ||
            p->p_vaddr != p->p_paddr || p->p_paddr < 0x100000 || p->p_paddr % 4096 ||
            p->p_memsz > (~(UINT64)0) - p->p_paddr - 4095)
        {
            status = EFI_LOAD_ERROR;
            goto done;
        }

        if ((p->p_flags & 1) && header.e_entry >= p->p_vaddr && header.e_entry - p->p_vaddr < p->p_memsz)
        {
            entryFound = 1;
        }
    }

    if (!entryFound)
    {
        status = EFI_LOAD_ERROR;
        goto done;
    }

    for (UINTN i = 0; i < header.e_phnum; ++i)
    {
        Elf64_Phdr* p = &segments[i];
        if (p->p_type != PT_LOAD || !p->p_memsz)
        {
            continue;
        }

        EFI_PHYSICAL_ADDRESS address = p->p_paddr;
        status = BS->AllocatePages(AllocateAddress, EfiLoaderData, (p->p_memsz + 4095) / 4096, &address);
        if (EFI_ERROR(status))
        {
            goto done;
        }

        SetMem((void*)address, p->p_memsz, 0);
        status = ReadAt(file, p->p_offset, (void*)address, p->p_filesz);
        if (EFI_ERROR(status))
        {
            goto done;
        }
    }

    *entry = header.e_entry;
done:
    FreePool(segments);
    return status;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* system)
{
    serialAvailable = HackosSerialInitialise();
    SerialLog("HackOS UEFI loader\n");
    InitializeLib(image, system);
    BS->SetWatchdogTimer(0, 0, 0, NULL);
    EFI_FILE* kernel = LoadFile(L"\\kernel.elf", image);
    if (!kernel)
    {
        SerialLog("UEFI: Cannot open kernel.elf\n");
        Print(L"Cannot open kernel.elf\n");
        return EFI_NOT_FOUND;
    }

    UINT64 entry;
    EFI_STATUS status = LoadKernel(kernel, &entry);
    kernel->Close(kernel);
    if (EFI_ERROR(status))
    {
        SerialLog("UEFI: Cannot load kernel\n");
        Print(L"Cannot load kernel: %r\n", status);
        return status;
    }

    BOOTINFO info = {0};
    info.font = LoadFont(image);
    if (!info.font)
    {
        SerialLog("UEFI: Cannot load the PSF1 font\n");
        Print(L"Cannot load the PSF1 font\n");
        return EFI_NOT_FOUND;
    }

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
    status = BS->LocateProtocol(&gopGuid, NULL, (void**)&gop);
    if (EFI_ERROR(status))
    {
        return status;
    }

    if (gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor)
    {
        SerialLog("UEFI: A 32-bit BGR framebuffer is required\n");
        Print(L"A 32-bit BGR framebuffer is required\n");
        return EFI_UNSUPPORTED;
    }

    info.framebuffer.address = (unsigned int*)gop->Mode->FrameBufferBase;
    info.framebuffer.width = gop->Mode->Info->HorizontalResolution;
    info.framebuffer.height = gop->Mode->Info->VerticalResolution;
    info.framebuffer.pitch = gop->Mode->Info->PixelsPerScanLine;
    info.framebuffer.bpp = 32;
    EFI_GUID acpi2 = ACPI_20_TABLE_GUID, acpi1 = ACPI_TABLE_GUID;
    for (UINTN i = 0; i < system->NumberOfTableEntries; ++i)
    {
        EFI_CONFIGURATION_TABLE* table = &system->ConfigurationTable[i];
        if (CompareGuid(&table->VendorGuid, &acpi2) == 0 ||
            (!info.rsdp && CompareGuid(&table->VendorGuid, &acpi1) == 0))
        {
            if (EqualBytes(table->VendorTable, "RSD PTR ", 8))
            {
                info.rsdp = table->VendorTable;
            }
        }
    }

    if (!info.rsdp)
    {
        SerialLog("UEFI: ACPI RSDP unavailable\n");
        Print(L"ACPI RSDP unavailable\n");
        return EFI_NOT_FOUND;
    }

    info.rsdp_revision = ((UINT8*)info.rsdp)[15];
    info.memMapEntrySize = sizeof(MemoryMapEntry);
    // The shared boot ABI has an 8-bit entry count. Reject overflow instead of truncating it.
    info.memMap = AllocatePool(255 * sizeof(MemoryMapEntry));
    if (!info.memMap)
    {
        SerialLog("UEFI: handoff allocation failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    SerialLog("UEFI: kernel loaded, leaving boot services\n");
    Print(L"Starting HackOS (%dx%d)\n", info.framebuffer.width, info.framebuffer.height);
    UINTN size = 0, key = 0, stride = 0;
    UINT32 version;
    status = BS->GetMemoryMap(&size, NULL, &key, &stride, &version);
    if (status != EFI_BUFFER_TOO_SMALL || stride < sizeof(EFI_MEMORY_DESCRIPTOR))
    {
        return EFI_LOAD_ERROR;
    }

    UINTN capacity = size + 32 * stride;
    EFI_MEMORY_DESCRIPTOR* map = AllocatePool(capacity);
    if (!map)
    {
        SerialLog("UEFI: memory map allocation failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        size = capacity;
        status = BS->GetMemoryMap(&size, map, &key, &stride, &version);
        if (EFI_ERROR(status) || stride < sizeof(*map) || size / stride > 255)
        {
            break;
        }

        info.memMapEntries = size / stride;
        for (UINTN i = 0; i < info.memMapEntries; ++i)
        {
            EFI_MEMORY_DESCRIPTOR* descriptor = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + i * stride);
            info.memMap[i].address = descriptor->PhysicalStart;
            info.memMap[i].length = descriptor->NumberOfPages * 4096;
            info.memMap[i].type = descriptor->Type;
            info.memMap[i].extended_attributes = descriptor->Attribute;
        }

        // No allocations, logging or protocol calls between the final map and ExitBootServices.
        status = BS->ExitBootServices(image, key);
        if (status == EFI_SUCCESS)
        {
            __asm__ volatile("cli");
            typedef int(__attribute__((sysv_abi)) * KernelEntry)(BOOTINFO*);
            ((KernelEntry)entry)(&info);
            break;
        }

        if (status != EFI_INVALID_PARAMETER)
        {
            break;
        }
    }

    SerialLog("UEFI: boot handoff failed or kernel returned\n");
    // Firmware services cannot be used after attempting ExitBootServices.
    for (;;)
    {
        __asm__ volatile("cli; hlt");
    }
}
