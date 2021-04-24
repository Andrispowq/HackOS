#include "ata.h"

#include "cpu/ports.h"

static void WaitBSY()
{
	while(__inb(0x1F7) & ATA_SR_BSY);
}
static void WaitDRQ()
{
	while(!(__inb(0x1F7) & ATA_SR_DRQ));
}

void ReadSectorsATA(uint64_t target_address, uint32_t LBA, uint8_t sector_count)
{
	WaitBSY();

	__outb(0x1F6,0xE0 | ((LBA >> 24) & 0xF));
	__outb(0x1F2, sector_count);
	__outb(0x1F3, (uint8_t) LBA);
	__outb(0x1F4, (uint8_t)(LBA >> 8));
	__outb(0x1F5, (uint8_t)(LBA >> 16)); 
	__outb(0x1F7, 0x20); //Send the read command

	uint16_t *target = (uint16_t*) target_address;

	for (int j = 0; j < sector_count;j ++)
	{
		WaitBSY();
		WaitDRQ();

		for(int i = 0; i < 256; i++)
			target[i] = __inw(0x1F0);

		target += 256;
	}
}

void WriteSectorsATA(uint32_t LBA, uint8_t sector_count, uint32_t* bytes)
{
	WaitBSY();

	__outb(0x1F6,0xE0 | ((LBA >> 24) & 0xF));
	__outb(0x1F2,sector_count);
	__outb(0x1F3, (uint8_t) LBA);
	__outb(0x1F4, (uint8_t)(LBA >> 8));
	__outb(0x1F5, (uint8_t)(LBA >> 16)); 
	__outb(0x1F7,0x30); //Send the write command

	for (int j = 0; j < sector_count; j++)
	{
		WaitBSY();
		WaitDRQ();

		for(int i = 0; i < 256; i++)
		{
			__outl(0x1F0, bytes[i]);
		}
	}
}