#include "stdio.h"
int main(int, char**)
{
    printf("WAIT_PROBE_READY\n");
    for (;;) asm volatile("pause");
}
