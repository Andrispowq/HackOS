#include "hackos/user.h"
#include "hackos/runtime/line_buffer.h"

int main()
{
    User::Print("Interactive input demo. Type exit to finish, or Ctrl-C to interrupt.\ninput> ");
    LineBuffer line;
    bool discard = false;
    for (;;)
    {
        uint64_t value = syscall_input();
        if (value == HACKOS_DENIED)
        {
            User::Print("Input requires a foreground launch.\n");
            return 1;
        }

        if (!value)
        {
            syscall_yield();
            continue;
        }

        if (value == HACKOS_INPUT_OVERFLOW)
        {
            line.Reset();
            discard = true;
            User::Print("\nInput overflow; press Enter.\n");
            continue;
        }

        if (discard)
        {
            if (value == '\n')
            {
                discard = false;
                User::Print("input> ");
            }

            continue;
        }

        if (value >= 128)
        {
            continue;
        }

        char ch = (char)value;
        switch (line.Push(ch))
        {
        case LineBuffer::Echo:
            syscall_user_write(&ch, 1);
            break;
        case LineBuffer::Erase:
            syscall_console(0);
            break;
        case LineBuffer::Submit:
            User::Print("\n");
            if (User::Equal(line.Text(), "exit"))
            {
                return 0;
            }

            User::Print("You typed: ");
            User::Print(line.Text());
            User::Print("\ninput> ");
            line.Reset();
            break;
        case LineBuffer::Overflow:
            User::Print("\nLine too long.\ninput> ");
            line.Reset();
            break;
        default:
            break;
        }
    }
}
