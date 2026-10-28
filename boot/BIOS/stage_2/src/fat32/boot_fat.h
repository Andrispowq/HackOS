#ifndef BOOT_FAT_H
#define BOOT_FAT_H
#include <stdint.h>

/* Sector callback returns 1 only after a complete 512-byte read. */
typedef int (*BootReadSector)(uint32_t lba, void* output);
int BootFatOpen(BootReadSector read);
uint32_t BootFatSize(void);
int BootFatRead(uint64_t offset, void* output, uint64_t size);
#endif
