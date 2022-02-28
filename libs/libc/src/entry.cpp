
#include "stdio.h"
#include "kernel_interface/syscall.h"

int main(int argc, char* argv[]);

extern "C" int _entry()
{
    int ret = main(0, nullptr);
    syscall_exit(ret);
}

extern "C" void __cxa_pure_virtual()
{
    printf("Virtual function definition not found!\n");
    syscall_exit(-1);
}

extern "C" void __stack_chk_fail()
{
    printf("Stack check failed!\n");
    syscall_exit(-1);
}