#ifndef FONT_H
#define FONT_H

#include "stdint.h"

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

#endif 