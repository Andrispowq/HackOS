
#include "stdint.h"

#include "kernel_interface/syscall.h"
#include "framebuffer/framebuffer.h"

#include "memory.h"
#include "string.h"
#include "stdio.h"

const char* _file_contents = "This is an example file put under USR/\nThe contents of this file are checked to see if the file API works.\n";

int main(int argc, char* argv[])
{
    uint64_t winID = syscall_create_window(200, 200, 0x1);
    Framebuffer* fb = (Framebuffer*)syscall_get_window_buffer(winID);
    if(!fb)
    {
        printf("ERROR");
        syscall_destroy_window(winID);
        return 1;
    }
    
    syscall_set_window_title(winID, "Yo yo yo whats up");
    //fb->DrawRect(10, 10, 20, 20, 0xFFFF00FF);

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