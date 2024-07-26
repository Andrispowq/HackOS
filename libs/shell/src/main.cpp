
#include "stdint.h"

#include "kernel_interface/syscall.h"
#include "screen/screen.h"
#include "file/file.h"

#include "memory.h"
#include "string.h"
#include "stdio.h"

char working_dir[256];

uint64_t initialise_window()
{
    uint64_t winID = syscall_create_window(200, 200, 0);
    syscall_set_window_title(winID, "Shell32.exe");
    return winID;
}

Framebuffer* initialise_buffer(uint64_t winID)
{
    Framebuffer* fb = (Framebuffer*)syscall_get_window_buffer(winID);
    fb->Clear(0xFFFFFFFF);
    return fb;
}

PSF1_FONT* load_font()
{
    ActiveFile* file = (ActiveFile*)syscall_fopen("fs0:/UTIL/FONTS/zap-light16.psf", 0x2);
    if(file == 0)
    {
        return 0;
    }

    PSF1_FONT* font = (PSF1_FONT*)malloc(file->GetSize());
    syscall_fread(font, 1, file->GetSize(), file);
    syscall_fclose(file);

    return font;
}

int main(int argc, char* argv[])
{
    printf("Yo kernel whats up\n");

    memset(working_dir, 0, 256);
    strcpy(working_dir, "~/");

    uint64_t winID = initialise_window();
    Framebuffer* fb = initialise_buffer(winID);
    PSF1_FONT* font = load_font();

    InitialiseDisplay(fb, font);
    
    /*Display::SharedDisplay()->puts("Hello");

    char* input = nullptr;
    while(true)
    {
        input = (char*)syscall_kread();
        if(input[0] == 0)
        {
            continue;
        }

        if(input[0] == 'q')
        {
            break;
        }

        Display::SharedDisplay()->putc(input[0]);
    }

    syscall_destroy_window(winID);*/

    return 0;
}