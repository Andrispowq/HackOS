#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

int main(int argc, char** args)
{
    printf("Welcome to BinDump! The dumped file:\n");

    if(argc != 3) //There are 3 arguments: ./bindump, -h or -b for hex or binary, and the file name
    {
        printf("ERROR: Please enter 2 valid arguments:\n\t-h: "
            "hexadecimal printage\n\t-b: binary printage\n"
            "and the file name!\n");
        return -1;
    }

    FILE* rstream = fopen(args[2], "rb");

    int length;
    fseek(rstream, 0, SEEK_END);
    length = ftell(rstream);
    length &= 0xFFFFFFFC;
    fseek(rstream, 0, 0);

    unsigned char* buffer = (unsigned char*) malloc(length);
    fread(buffer, 1, length, rstream);

    fclose(rstream);

    if(strcmp(args[1], "-h") == 0)
    {
        int n = length / 16; //Print 16 bytes per line
        for(int i = 0; i < n; i++)
        {
            printf("%#08x: ", i * 16);
            unsigned char* ascii = malloc(17);
            for(int j = 0; j < 16; j++)
            {
                unsigned char byte = buffer[i * 16 + j];
                if(byte == '\n' || byte == '\t' || byte == '\v' || byte == '\b'
                    || byte == '\0')
                {
                    ascii[j] = '.'; 
                } 
                else
                {
                    ascii[j] = byte;
                }

                printf("%02x ", byte);
            }

            ascii[16] = 0;

            printf("\t%s\n", ascii);
        }
    }
    else if(strcmp(args[1], "-b") == 0)
    {
        int n = length / 6; //Print 6 bytes per line
        for(int i = 0; i < n; i++)
        {
            printf("%#08x: ", i * 6);
            unsigned char* ascii = malloc(7);
            for(int j = 0; j < 6; j++)
            {
                unsigned char byte = buffer[i * 6 + j];
                if(byte == '\n' || byte == '\t' || byte == '\v' || byte == '\b'
                    || byte == '\0')
                {
                    ascii[j] = '.'; 
                } 
                else
                {
                    ascii[j] = byte;
                }

                printf(BYTE_TO_BINARY_PATTERN" ", BYTE_TO_BINARY(byte));
            }

            ascii[16] = 0;

            printf("\t%s\n", ascii);
        }
    }
    else
    {
        printf("ERROR: The second argument must either be:\n"
            "\t-h: Hexadecimal printage\n\t-b: Binary printage\n"
            "while you entered: %s\n", args[1]);
        return -1;
    }
    

    return 0;
}