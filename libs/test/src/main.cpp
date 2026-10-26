#include "hackos/user.h"
int main(int argc, char** argv)
{
    for (int i = 0; i < argc; ++i)
    { User::Print("argv["); User::Number(i); User::Print("]="); User::Print(argv[i]); User::Print("\n"); }
    User::Print("Hello from the standalone ring-3 test program.\n");
    uint64_t handle = syscall_open("~/USR/HELLO.TXT");
    if (handle == HACKOS_ERROR) return 1;
    char buffer[512];
    uint64_t count = syscall_read(handle, buffer, sizeof(buffer));
    if (count != HACKOS_ERROR) syscall_user_write(buffer, count);
    syscall_close(handle);
    return count == HACKOS_ERROR ? 1 : 0;
}
