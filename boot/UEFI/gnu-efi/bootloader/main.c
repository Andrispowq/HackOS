#include <efi.h>
#include <efilib.h>
#include <elf.h>

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

int memcmp(const void* aptr, const void* bptr, size_t n)
{
	const unsigned char* a = aptr, *b = bptr;

	for (size_t i = 0; i < n; i++)
	{
		if (a[i] < b[i]) 
			return -1;
		else if (a[i] > b[i]) 
			return 1;
	}

	return 0;
}

UINTN strcmp(CHAR8* a, CHAR8* b, UINTN length)
{
	for (UINTN i = 0; i < length; i++)
	{
		if (*a != *b) 
			return 0;
	}

	return 1;
}

void _memset(void* dst, int v, size_t n)
{
	for(UINTN i = 0; i < n; i++)
	{
		((uint8_t*)dst)[i] = (uint8_t)v;
	}
}

Framebuffer framebuffer;
Framebuffer* InitializeGOP()
{
	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	EFI_STATUS status;

	status = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
	if(EFI_ERROR(status))
	{
		Print(L"Unable to locate Graphics Output Protocol!\n");
		return NULL;
	}
	else
	{
		Print(L"Graphics Output Protocol located!\n");
	}

	framebuffer.address = (unsigned int*)gop->Mode->FrameBufferBase;
	framebuffer.width = gop->Mode->Info->HorizontalResolution;
	framebuffer.height = gop->Mode->Info->VerticalResolution;
	framebuffer.bpp = 32;
	framebuffer.pitch = gop->Mode->Info->PixelsPerScanLine;

	return &framebuffer;	
}

EFI_FILE* LoadFile(EFI_FILE* Directory, CHAR16* Path, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
	EFI_FILE* LoadedFile;

	EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
	SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&LoadedImage);

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
	SystemTable->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&FileSystem);

	if (Directory == NULL)
	{
		FileSystem->OpenVolume(FileSystem, &Directory);
	}

	EFI_STATUS s = Directory->Open(Directory, &LoadedFile, Path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (s != EFI_SUCCESS)
	{
		return NULL;
	}

	return LoadedFile;
}

PSF1_FONT* LoadPSF1Font(EFI_FILE* Directory, CHAR16* Path, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
	EFI_FILE* font = LoadFile(Directory, Path, ImageHandle, SystemTable);
	if (font == NULL) 
		return NULL;

	PSF1_HEADER* fontHeader;
	SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(PSF1_HEADER), (void**)&fontHeader);
	UINTN size = sizeof(PSF1_HEADER);
	font->Read(font, &size, fontHeader);

	if (fontHeader->magic[0] != PSF1_MAGIC0 || fontHeader->magic[1] != PSF1_MAGIC1)
	{
		return NULL;
	}

	UINTN glyphBufferSize = fontHeader->charsize * 256;
	if (fontHeader->mode == 1) 
	{
		glyphBufferSize = fontHeader->charsize * 512;
	}

	void* glyphBuffer;
	{
		font->SetPosition(font, sizeof(PSF1_HEADER));
		SystemTable->BootServices->AllocatePool(EfiLoaderData, glyphBufferSize, (void**)&glyphBuffer);
		font->Read(font, &glyphBufferSize, glyphBuffer);
	}

	PSF1_FONT* finishedFont;
	SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(PSF1_FONT), (void**)&finishedFont);
	finishedFont->psf1_Header = fontHeader;
	finishedFont->glyphBuffer = glyphBuffer;
	return finishedFont;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) 
{
	InitializeLib(ImageHandle, SystemTable);
	Print(L"Initialised the EFI library!\n");

	EFI_FILE* Kernel = LoadFile(NULL, L"kernel.elf", ImageHandle, SystemTable);
	if (Kernel == NULL)
	{
		Print(L"Could not load kernel!\n");
	}

	Elf64_Ehdr header;
	{
		UINTN FileInfoSize;
		EFI_FILE_INFO* FileInfo;
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, NULL);
		SystemTable->BootServices->AllocatePool(EfiLoaderData, FileInfoSize, (void**)&FileInfo);
		Kernel->GetInfo(Kernel, &gEfiFileInfoGuid, &FileInfoSize, (void**)&FileInfo);

		UINTN size = sizeof(header);
		Kernel->Read(Kernel, &size, &header);
	}

	if (
		memcmp(&header.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0 ||
		header.e_ident[EI_CLASS] != ELFCLASS64 ||
		header.e_ident[EI_DATA] != ELFDATA2LSB ||
		header.e_type != ET_EXEC ||
		header.e_machine != EM_X86_64 ||
		header.e_version != EV_CURRENT
	)
	{
		Print(L"Kernel format is bad!\n");
	}

	Elf64_Phdr* phdrs;
	{
		Kernel->SetPosition(Kernel, header.e_phoff);
		UINTN size = header.e_phnum * header.e_phentsize;
		SystemTable->BootServices->AllocatePool(EfiLoaderData, size, (void**)&phdrs);
		Kernel->Read(Kernel, &size, phdrs);
	}

	for (
		Elf64_Phdr* phdr = phdrs;
		(char*)phdr < (char*)phdrs + header.e_phnum * header.e_phentsize;
		phdr = (Elf64_Phdr*)((char*)phdr + header.e_phentsize)
	)
	{
		switch (phdr->p_type)
		{
			case PT_LOAD:
			{
				int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
				Elf64_Addr segment = phdr->p_paddr;
				SystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);
				_memset((void*)segment, 0, phdr->p_filesz);

				Kernel->SetPosition(Kernel, phdr->p_offset);
				UINTN size = phdr->p_filesz;
				Kernel->Read(Kernel, &size, (void*)segment);
				break;
			}
		}
	}

	PSF1_FONT* newFont = LoadPSF1Font(NULL, L"zap-light16.psf", ImageHandle, SystemTable);
	if (newFont == NULL)
	{
		Print(L"Font is not valid or is not found!\n");
	}

	Framebuffer* newBuffer = InitializeGOP();

	EFI_MEMORY_DESCRIPTOR* Map = NULL;
	UINTN MapSize, MapKey;
	UINTN DescriptorSize;
	UINT32 DescriptorVersion;
	{
		
		SystemTable->BootServices->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);
		SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, (void**)&Map);
		SystemTable->BootServices->GetMemoryMap(&MapSize, Map, &MapKey, &DescriptorSize, &DescriptorVersion);

	}

	EFI_CONFIGURATION_TABLE* configTable = SystemTable->ConfigurationTable;
	void* rsdp = NULL; 
	EFI_GUID Acpi2TableGuid = ACPI_20_TABLE_GUID;

	for (UINTN index = 0; index < SystemTable->NumberOfTableEntries; index++)
	{
		if (CompareGuid(&configTable[index].VendorGuid, &Acpi2TableGuid))
		{
			if (strcmp((CHAR8*)"RSD PTR ", (CHAR8*)configTable->VendorTable, 8))
			{
				rsdp = (void*)configTable->VendorTable;
			}
		}
		configTable++;
	}

	Print(L"Address: 0x%x, Base: 0x%x\nSize: %d * %d\n",
	newBuffer,
	newBuffer->address, 
	newBuffer->width, 
	newBuffer->height);

	uint64_t memMapEntries = MapSize / DescriptorSize;

	MemoryMapEntry* entries;
	SystemTable->BootServices->AllocatePool(EfiLoaderData, sizeof(MemoryMapEntry) * memMapEntries, (void**)&entries);
	
	Print(L"Memory map start address: 0x%x, size: %d, count: %d\n", Map, DescriptorSize, MapSize / DescriptorSize);

	for(UINTN i = 0; i < memMapEntries; i++)
	{
		EFI_MEMORY_DESCRIPTOR* curr = (EFI_MEMORY_DESCRIPTOR*)((uint64_t)Map + DescriptorSize * i);
		
		MemoryMapEntry* entry = entries + i;
		entry->address = curr->PhysicalStart;
		entry->length = curr->NumberOfPages * 0x1000;
		entry->type = curr->Type;
		entry->extended_attributes = (uint32_t)curr->Attribute;
	}

	BOOTINFO info;
	info.framebuffer = *newBuffer;
	info.font = newFont;
	info.memMap = entries;
	info.memMapEntries = memMapEntries;
	info.rsdp = rsdp;
	info.memMapEntrySize = sizeof(MemoryMapEntry);
	info.rsdp_revision = 1;
	info.booted_from_BIOS = 0;

	SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);

	int(*KernelStart)(BOOTINFO*) = ((__attribute__((sysv_abi)) int(*)(BOOTINFO*)) header.e_entry);
	int ret = KernelStart(&info);

	Print(L"Kernel returned with code %d\n", ret);

	return EFI_SUCCESS;
}