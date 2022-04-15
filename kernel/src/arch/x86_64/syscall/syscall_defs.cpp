#include "syscall_defs.h"

#include "lib/memory.h"
#include "lib/string.h"

char user_input[256];

void exit(int code)
{
    _Kill();
}

char* kread()
{
    strcpy(user_input, system_input);    
    memset(system_input, 0, 256);

    return user_input;
}