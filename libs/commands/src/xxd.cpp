#include "files.h"

static char contents[HACKOS_FILE_LIMIT];

static char Hex(unsigned value)
{
    return "0123456789abcdef"[value & 15];
}

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "xxd <file>"))
    {
        return 2;
    }

    uint64_t size;
    if (LoadWholeFile(argv[1], contents, size))
    {
        Error("xxd: cannot read file.\n");
        return 1;
    }

    Print("OFFSET    HEX          BINARY                                TEXT\n");
    for (uint64_t offset = 0; offset < size; offset += 4)
    {
        char line[80];
        unsigned used = 0;
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            line[used++] = Hex(offset >> shift);
        }

        line[used++] = ':';
        line[used++] = ' ';
        for (unsigned i = 0; i < 4; ++i)
        {
            bool present = offset + i < size;
            uint8_t byte = present ? contents[offset + i] : 0;
            line[used++] = present ? Hex(byte >> 4) : ' ';
            line[used++] = present ? Hex(byte) : ' ';
            line[used++] = ' ';
        }

        line[used++] = ' ';
        for (unsigned i = 0; i < 4; ++i)
        {
            for (int bit = 7; bit >= 0; --bit)
            {
                line[used++] = offset + i < size ? '0' + (((uint8_t)contents[offset + i] >> bit) & 1) : ' ';
            }

            line[used++] = ' ';
        }

        line[used++] = '|';
        for (unsigned i = 0; i < 4 && offset + i < size; ++i)
        {
            uint8_t byte = contents[offset + i];
            line[used++] = byte >= 32 && byte <= 126 ? byte : '.';
        }

        line[used++] = '|';
        line[used++] = '\n';
        syscall_user_write(line, used);
    }

    return 0;
}
