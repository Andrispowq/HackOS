
#include "stdint.h"

#include "kernel_interface/syscall.h"

#include "memory.h"
#include "string.h"
#include "stdio.h"

int check_command(char* cmd, const char* text);
int check_short_command(char* cmd, const char* text, int length);

char working_dir[256];

void process_command(char* input)
{
    if(check_command(input, "help"))
    {
        printf("This is the HackOS help panel!\nCurrently implemented commands:\n");
        printf("\t- clear: Clears the console\n");
        printf("\t- page: Allocates 4096 bytes of page-aligned memory (kmalloc test)\n");
        printf("\t- calc: Enters calculator mode, which is currently not functioning\n");
        printf("\t- print <arg>: Prints <arg> to the console\n");
        printf("\t- print <var>: Prints <val> to the console, if it's a kernel variable\n");
        printf("\t- shutdown: Halts the CPU, causing the computer to shut down\n");
        printf("\t- restart: Restarts the computer (only works on Virtual Machines)\n");
        printf("\t- help: displays this menu\n");
    }
    else if(check_command(input, "quit"))
    {
        syscall_exit(0);
    }

    printf("root@root:%s$ ", working_dir);
}

int main(int argc, char* argv[])
{
    memset(working_dir, 0, 256);
    strcpy(working_dir, "~/");

    uint64_t winID = syscall_create_window(200, 200, 0);

    char* input = nullptr;
    while(true)
    {
        input = (char*)syscall_kread();
        if(input[0] == 0)
        {
            continue;
        }

        printf("Recieved command %s\n", input);

        process_command(input);
    }

    syscall_destroy_window(winID);

    return 0;
}

int check_command(char* cmd, const char* text)
{
    return strcmp(cmd, (char*)text) == 0;
}

int check_short_command(char* cmd, const char* text, int length)
{
    int i;
    for (i = 0; cmd[i] == text[i] && i < length; i++);

    if(i == length)
        return 1;
    else 
        return 0;
}