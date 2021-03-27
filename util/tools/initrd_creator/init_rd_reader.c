#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

typedef struct init_rd_header
{
    unsigned char magic; //Check for consistency
    char name[64]; //Filenames can't be longer than 64 characters
    unsigned int offset; //Offset to the file start
    unsigned int length; //File length
} init_rd_header_t;

int main(int argc, char** args)
{
    const int max_headers = 64;
    int n_headers = (argc - 1) / 2;
    int filec; //Number of files
    init_rd_header_t headers[max_headers]; //max headers: 64 -> at most 64 files

    if(argc != 2)
    {
        printf("ERROR: argc unmatched! Please enter a valid filenaame for which we will read the contents!\n");
    }

    FILE* rstream = fopen(args[1], "r");
    fread(&filec, sizeof(int), 1, rstream); //read number of files
    fread(headers, sizeof(init_rd_header_t), max_headers, rstream);

    unsigned int offset = sizeof(init_rd_header_t) * max_headers;

    for(int i = 0; i < filec; i++)
    {
        printf("Printing data of file %d:\n", i);
        printf("\tmagic number: 0x%08x\n\tname: %s\n\toffset: 0x%08x bytes\n\tlength: 0x%08x bytes\n",
            headers[i].magic, headers[i].name, headers[i].offset, headers[i].length);

        offset += headers[i].offset;
        char* buff = malloc(headers[i].length);
        fread(buff, 1, headers[i].length, rstream);

        printf("%s\n\n", buff);
    }

    return 0;
}