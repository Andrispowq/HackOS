#include "output.h"

static const char* keys[] = {"font_size", "bg_col", "font_col", "columns", "rows"};

static int Usage(bool help)
{
    const char* text = "Usage: terminal get [key] | terminal set <key> <value> | terminal reset\n"
                       "  font_size  Font height in pixels: even numbers 8..64 (window default 16; console 30).\n"
                       "  bg_col     Background colour as 0xRRGGBBAA (window 0x101b25ff; console 0x000000ff).\n"
                       "  font_col   Text colour as 0xRRGGBBAA (window 0xe5eef5ff; console 0xffffffff).\n"
                       "  columns, rows  Read-only terminal dimensions in characters.\n"
                       "get without a key lists all values. set also accepts 'default'.\n"
                       "Colours accept eight hex digits with optional 0x prefix. Alpha blends\n"
                       "the background over black, then text over that background.\n"
                       "Changes clear screen/history, require foreground ownership, and last\n"
                       "until reset/terminal close (console: reboot). reset restores defaults. Use --help for help.\n";
    bool written = Write(help ? 1 : 2, text, Length(text));
    return help ? (written ? 0 : 1) : 2;
}

static int Key(const char* name)
{
    for (int i = 0; i < 5; ++i)
    {
        if (Equal(name, keys[i]))
        {
            return i;
        }
    }
    return -1;
}

static bool Value(int key, const char* text, uint32_t& value)
{
    value = 0;
    if (key == 0)
    {
        if (!*text)
        {
            return false;
        }
        for (; *text; ++text)
        {
            if (*text < '0' || *text > '9' || value > 64)
            {
                return false;
            }
            value = value * 10 + (*text - '0');
        }
        return value >= 8 && value <= 64 && value % 2 == 0;
    }

    if (Starts(text, "0x") || Starts(text, "0X"))
    {
        text += 2;
    }
    if (Length(text) != 8)
    {
        return false;
    }
    for (unsigned i = 0; i < 8; ++i)
    {
        char ch = text[i];
        unsigned digit;
        if (ch >= '0' && ch <= '9')
        {
            digit = ch - '0';
        }
        else if (ch >= 'a' && ch <= 'f')
        {
            digit = ch - 'a' + 10;
        }
        else if (ch >= 'A' && ch <= 'F')
        {
            digit = ch - 'A' + 10;
        }
        else
        {
            return false;
        }
        value = (value << 4) | digit;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc == 2 && Equal(argv[1], "--help"))
    {
        return Usage(true);
    }
    if ((argc == 2 || argc == 3) && Equal(argv[1], "get"))
    {
        int key = argc == 3 ? Key(argv[2]) : -1;
        if (argc == 3 && key < 0)
        {
            return Usage(false);
        }
        HackosTerminalSettings settings = {};
        if (syscall_terminal_get(&settings) != 0)
        {
            Error("terminal: cannot read settings\n");
            return 1;
        }
        uint32_t values[] = {settings.fontSize, settings.bgColour, settings.fontColour, settings.columns,
                             settings.rows};
        ReportOutput out;
        for (int i = 0; i < 5; ++i)
        {
            if (key >= 0 && i != key)
            {
                continue;
            }
            if (key < 0)
            {
                out.Text(keys[i]);
                out.Text(" = ");
            }
            bool colour = i == 1 || i == 2;
            if (colour)
            {
                out.Text("0x");
            }
            out.Number(values[i], colour ? 16 : 10, colour ? 8 : 0);
            out.Text("\n");
        }
        return out.ok ? 0 : 1;
    }

    uint64_t operation = HACKOS_TERMINAL_RESET;
    uint32_t value = 0;
    if (argc == 4 && Equal(argv[1], "set"))
    {
        int key = Key(argv[2]);
        if (key < 0 || key >= 3 || (!Equal(argv[3], "default") && !Value(key, argv[3], value)))
        {
            return Usage(false);
        }
        operation = key + 1;
        if (Equal(argv[3], "default"))
        {
            operation = HACKOS_TERMINAL_DEFAULT;
            value = key + 1;
        }
    }
    else if (argc != 2 || !Equal(argv[1], "reset"))
    {
        return Usage(false);
    }

    uint64_t result = syscall_terminal_set(operation, value);
    if (result != 0)
    {
        Error(result == HACKOS_DENIED ? "terminal: foreground terminal required\n"
                                      : "terminal: cannot apply setting\n");
        return 1;
    }
    return 0;
}
