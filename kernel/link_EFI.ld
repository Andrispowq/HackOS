OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)
. = 0x00100000;

SECTIONS
{
    kernel_start = .;
    
    .text : ALIGN(0x1000)
	{
		*(.text)
	}

	.data : ALIGN(0x1000)
	{
		*(.data)
	}

	.rodata : ALIGN(0x1000)
	{
		*(.rodata)
	}

	bss_start = .;

	.bss : ALIGN(0x1000)
	{
		*(COMMON)
		*(.bss)
	}

	bss_end = .;

    end = .; _end = .; __end = .;
}
