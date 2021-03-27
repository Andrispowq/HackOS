#ifndef INITRD_H
#define INITRD_H

#include "../libc/stdint.h"
#include "fs.h"

typedef struct initrd_header
{
    uint32_t nfiles; // The number of files in the ramdisk.
} initrd_header_t;

typedef struct initrd_file_header
{
    uint8_t magic;     // Magic number, for error checking.
    char name[64];      // Filename.
    uint32_t offset;   // Offset in the initrd that the file starts.
    uint32_t length;   // Length of the file.
} initrd_file_header_t;

// Initialises the initial ramdisk. It gets passed the address of the multiboot module,
// and returns a completed filesystem node.
fs_node_t* initialise_initrd(uint32_t location);

#endif