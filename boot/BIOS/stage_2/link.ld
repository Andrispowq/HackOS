OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
. = 0x00008000;

SECTIONS
{
    loader_start = .;

	.boot :
	{
		*(.entry)
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

ASSERT(SIZEOF(.boot) <= 0x1000, "BIOS real-mode bridge exceeds low-memory entry page")
ASSERT(ADDR(.data) + SIZEOF(.data) <= 0x10000, "BIOS stage 2 exceeds its 32 KiB disk reservation")
ASSERT(end + 8192 < 0x20000, "BIOS loader overlaps disk bounce buffer")
