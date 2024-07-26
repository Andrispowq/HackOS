OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
. = 0x00008000;

SECTIONS
{
    loader_start = .;

	.boot :
	{
		*(.must_be_top)
	}

    .text : ALIGN(0x1000)
    {
        *(.text)
    }

    .rodata : ALIGN(0x1000)
    {
        *(.rodata)
    }

    .data : ALIGN(0x1000)
    {
        *(.data)
    }

    bss_start = .;

    .bss : ALIGN(0x1000)
    {
        *(.bss)
    }

    bss_end = .;

    end = .; _end = .; __end = .;
}
