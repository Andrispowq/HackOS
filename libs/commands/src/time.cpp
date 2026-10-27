#include "command.h"
#include "output.h"

int main(int argc, char** argv)
{
    unsigned fields = argc == 1 ? 7 : 0;
    for (int i = 1; i < argc; ++i)
    {
        if (Equal(argv[i], "--date") || Equal(argv[i], "-d"))
        {
            fields |= 1;
        }
        else if (Equal(argv[i], "--time") || Equal(argv[i], "-t"))
        {
            fields |= 2;
        }
        else if (Equal(argv[i], "--ticks") || Equal(argv[i], "-k"))
        {
            fields |= 4;
        }
        else if (Equal(argv[i], "--all") || Equal(argv[i], "-a"))
        {
            fields = 7;
        }
        else
        {
            bool help = argc == 2 && (Equal(argv[i], "--help") || Equal(argv[i], "-h"));
            const char* usage = "Usage: time [-d|--date] [-t|--time] [-k|--ticks] [-a|--all]\n"
                                "Default: date, time and ticks since kernel timer startup.\n"
                                "Clock: hardware RTC, one-second resolution; no timezone conversion.\n";
            if (help)
            {
                Print(usage);
            }
            else
            {
                Error(usage);
            }

            return help ? 0 : 2;
        }
    }

    HackosClock value = {};
    if (syscall_clock(&value))
    {
        Error("time: clock unavailable.\n");
        return 1;
    }

    ReportOutput out;
    if (fields & 1)
    {
        out.Number(value.year, 10, 4);
        out.Text("-");
        out.Number(value.month, 10, 2);
        out.Text("-");
        out.Number(value.day, 10, 2);
    }

    if (fields & 2)
    {
        if (fields & 1)
        {
            out.Text(" ");
        }
        out.Number(value.hour, 10, 2);
        out.Text(":");
        out.Number(value.minute, 10, 2);
        out.Text(":");
        out.Number(value.second, 10, 2);
    }

    if (fields & 4)
    {
        if (fields != 4)
        {
            out.Text(" | ");
        }
        out.Number(value.ticks);
        if (fields != 4)
        {
            out.Text(" ticks");
        }
    }

    out.Text("\n");
    return out.ok ? 0 : 1;
}
