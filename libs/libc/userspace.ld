OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
. = 0x100000000;

SECTIONS
{
	.text : ALIGN(4K)
	{
		*(.text)
	}

	.rodata : ALIGN(4K)
	{
		*(.rodata)
	}

	.data : ALIGN(4K)
	{
		*(.data)
	}

	.bss : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}
}