#include <stdio.h>

#define MAGIC 0xBF

typedef struct init_rd_header
{
    unsigned char magic; //Check for consistency
    char name[64]; //Filenames can't be longer than 64 characters
    unsigned int offset; //Offset to the file start
    unsigned int length; //File length
} init_rd_header_t;

//Format: ./init_rd_creator path/to/file1 file1_name_in_init_rd path/to/file2 file2_name_in_init_rd
int main(int argc, char** argv)
{
    const int max_headers = 64;
    int n_headers = (argc - 1) / 2;
    init_rd_header_t headers[max_headers]; //max headers: 64 -> at most 64 files

    //Headers + number of files
    unsigned int offset = sizeof(init_rd_header_t) * max_headers + sizeof(int);
    int i;
    for(i = 0; i < n_headers; ++i)
    {
        char* path = argv[i * 2 + 1];
        char* name = argv[i * 2 + 2];
        printf("Writing file %s -> %s at %x\n", path, name, offset);
        strcpy(headers[i].name, name);
        headers[i].offset = offset;

        FILE* stream = fopen(path, "r");
        if(!stream)
        {
            printf("ERROR: file (%s) not found!\n", path);
            return -1;
        }

        fseek(stream, 0, SEEK_END);
        headers[i].length = ftell(stream);
        offset += headers[i].length;
        fclose(stream);
        headers[i].magic = MAGIC;
    }

    FILE* wstream = fopen("./initrd.img", "w");
    unsigned char* data = (unsigned char*) malloc(offset);
    fwrite(&n_headers, sizeof(int), 1, wstream);
    fwrite(headers, sizeof(init_rd_header_t), max_headers, wstream);

    for(i = 0; i < n_headers; ++i)
    {
        char* path = argv[i * 2 + 1];
        FILE* stream = fopen(path, "r");
        unsigned char* buffer = (unsigned char*) malloc(headers[i].length);
        fread(buffer, 1, headers[i].length, stream);
        fwrite(buffer, 1, headers[i].length, wstream);
        fclose(stream);
        free(buffer);
    }

    fclose(wstream);
    free(data);

    return 0;
}