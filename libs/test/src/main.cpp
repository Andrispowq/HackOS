
#include "stdint.h"

#include "kernel_interface/syscall.h"

#include "memory.h"
#include "string.h"
#include "stdio.h"

const char* _file_contents = "This is an example file put under USR/\nThe contents of this file are checked to see if the file API works.\n";

int main(int argc, char* argv[])
{
    for(int i = 0; i < argc; i++)
    {
        printf("%i: %s\n", i, argv[i]);
    }

    char buff[128] = { 0 };

    void* file = (void*)syscall_fopen("fs0:/USR/HELLO_2.TXT", 1 /*O_CREAT*/);
    syscall_fread(buff, 128, 1, file);
    if(buff[0] == 0)
    {
        printf("File is empty, writing contents now.\n");
        syscall_fwrite((void*)_file_contents, strlen((char*)_file_contents), 1, file);
    }
    syscall_fclose(file);

    printf(buff);
    return 0;
}