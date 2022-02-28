
#include "stdint.h"

#include "kernel_interface/syscall.h"

#include "memory.h"
#include "stdio.h"

int main(int argc, char* argv[])
{
    char* kinput = (char*)syscall_kread();
    while(kinput[0] == 0)
    {
        char* kinput = (char*)syscall_kread();
    }

    printf("Input: %s\n", kinput);
    return 0;
}