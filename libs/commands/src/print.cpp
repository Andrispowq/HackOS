#include "command.h"

int main(int argc, char** argv)
{
    const char* text = argc == 2 ? argv[1] : "";
    if (Equal(text, "tick") || Equal(text, "time") || Equal(text, "date"))
    {
        HackosClock time;
        if (syscall_clock(&time))
        {
            Print("Clock unavailable.\n");
            return 1;
        }

        if (Equal(text, "tick"))
        {
            Print("Ticks: ");
            Number(time.ticks);
        }
        else if (Equal(text, "time"))
        {
            Print("Time: ");
            Number(time.hour, 16);
            Print(":");
            Number(time.minute, 16);
            Print(":");
            Number(time.second, 16);
        }
        else
        {
            Print("Date: 20");
            Number(time.year, 16);
            Print(".");
            Number(time.month, 16);
            Print(".");
            Number(time.day, 16);
        }
    }
    else
    {
        for (int i = 1; i < argc; ++i)
        {
            if (i > 1) Print(" ");
            Print(argv[i]);
        }
    }

    Print("\n");
    return 0;
}
