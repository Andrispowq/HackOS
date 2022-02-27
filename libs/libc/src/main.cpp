
#include "stdint.h"

#include "kernel_interface/syscall.h"

int main(int argc, char* argv[])
{
    syscall_kprintf("Hello world!\n");
    return 0x123;
}

extern "C" int _entry()
{
    return main(0, nullptr);
}