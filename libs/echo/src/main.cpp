
#include "stdint.h"

#include "kernel_interface/syscall.h"

#include "memory.h"
#include "stdio.h"

int main(int argc, char* argv[])
{
    printf("Hello world!\n");

    int* ptr = (int*)malloc(4);
    printf("Ptr: 0x%x\n", ptr);
    *ptr = 0xDEADBEEF;
    free(ptr); 

    return 0x123;
}