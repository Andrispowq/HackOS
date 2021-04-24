#include "font.h"

#include "libc/memory.h"

PSF1_FONT* LoadFont(void* address)
{
    uint64_t ptr = kmalloc(512 * 12);
    memcpy((void*)ptr, address, 512 * 12);

    //Check if this is indeed our font file
    PSF1_HEADER* font_hdr = (PSF1_HEADER*)ptr;
    if (font_hdr->magic[0] != PSF1_MAGIC0 || font_hdr->magic[1] != PSF1_MAGIC1)
	{
        asm("hlt");
	}

    //Build the font structure
	PSF1_FONT* finishedFont = (PSF1_FONT*)kmalloc(sizeof(PSF1_FONT));
	finishedFont->psf1_Header = font_hdr;
	finishedFont->glyphBuffer = (void*)(ptr + sizeof(PSF1_HEADER));

    return finishedFont;
}