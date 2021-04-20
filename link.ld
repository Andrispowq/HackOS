OUTPUT_FORMAT(elf64-x86-64)
ENTRY(main)
. = 0x01000000;

SECTIONS
{
    start = .;
    
    .text : ALIGN(0x1000)
	{
        code = .; _code = .; __code = .;
		*(.text)
	}

	.data : ALIGN(0x1000)
	{
        data = .; _data = .; __data = .;
		*(.data)
	}

	.rodata : ALIGN(0x1000)
	{
        rodata = .; _rodata = .; __rodata = .;
		*(.rodata)
	}

	bss_start = .;

	.bss : ALIGN(0x1000)
	{
        bss = .; _bss = .; __bss = .;
		*(.bss)
	}

	bss_end = .;

    end = .; _end = .; __end = .;
}