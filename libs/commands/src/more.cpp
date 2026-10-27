#include "filter.h"

class Pager
{
  public:
    FilterOutput output;
    unsigned columns = 80, page = 23, remaining = 23, column = 0;
    bool quit = false, failed = false;

    bool Pause()
    {
        if (remaining || quit || failed || output.error)
        {
            return !quit && !failed && !output.error;
        }
        const char* prompt = "--More--";
        output.Text(prompt);
        if (!output.Flush())
        {
            return false;
        }
        while (true)
        {
            uint64_t key = syscall_terminal_read(0);
            if (key == 'q' || key == 'Q' || key == 4)
            {
                quit = true;
                break;
            }
            if (key == ' ' || key == '\n' || key == '\r')
            {
                remaining = key == ' ' ? page : 1;
                break;
            }
            if (key == HACKOS_ERROR || key == HACKOS_DENIED)
            {
                failed = true;
                break;
            }
        }
        // Ctrl+Z/fg can move the cursor to a new row while this read sleeps.
        // Clear the current prompt row without backspacing into shell output.
        output.Byte('\r');
        for (unsigned i = 0; i < Length(prompt); ++i)
        {
            output.Byte(' ');
        }
        output.Byte('\r');
        output.Flush();
        return !quit && !failed && !output.error;
    }

    void Glyph(char ch)
    {
        if (!Pause())
        {
            return;
        }
        output.Byte(ch);
        if (ch == '\n' || ++column == columns)
        {
            column = 0;
            --remaining;
        }
    }

    void Display(unsigned char ch)
    {
        if (ch == '\t')
        {
            do
            {
                Glyph(' ');
            } while (column % 4 && !quit && !failed && !output.error);
        }
        else if (ch == '\n' || (ch >= 32 && ch < 127))
        {
            Glyph(ch);
        }
        else if (ch < 32 || ch == 127)
        {
            Glyph('^');
            Glyph(ch ^ 64);
        }
        else
        {
            Glyph('?');
        }
    }
};

int main(int argc, char** argv)
{
    const char* usage = "Usage: more [-n lines] [--] [file ...]\n"
                        "Space: next page; Enter: next line; q: quit. Ctrl+Z/C: stop/interrupt.\n"
                        "Redirected output is copied unchanged without prompts.\n";
    unsigned page = 0;
    int first = 1;
    for (; first < argc; ++first)
    {
        if (Equal(argv[first], "--help"))
        {
            return FilterUsage(usage, true);
        }
        if (Equal(argv[first], "--"))
        {
            ++first;
            break;
        }
        if (argv[first][0] != '-' || !argv[first][1])
        {
            break;
        }
        if (!Starts(argv[first], "-n"))
        {
            return FilterUsage(usage);
        }
        const char* number = argv[first] + 2;
        if (!*number)
        {
            if (++first == argc)
            {
                return FilterUsage(usage);
            }
            number = argv[first];
        }
        uint64_t value;
        if (!Decimal(number, value) || !value || value > 10000)
        {
            return FilterUsage(usage);
        }
        page = value;
    }

    HackosTerminalInfo terminal = {}, standard = {};
    if (syscall_terminal_info(1, &terminal))
    {
        FilterError("more", nullptr, "cannot inspect stdout");
        return 1;
    }
    syscall_terminal_info(0, &standard);
    bool interactive = (terminal.flags & HACKOS_TERMINAL) != 0;
    if (interactive && !(terminal.flags & HACKOS_TERMINAL_FOREGROUND))
    {
        FilterError("more", nullptr, "foreground terminal required");
        return 1;
    }

    FilterInputs inputs;
    Pager pager;
    pager.columns = terminal.columns ? terminal.columns : 80;
    pager.page = page ? page : terminal.rows > 1 ? terminal.rows - 1 : 23;
    pager.remaining = pager.page;
    bool ok = true;
    int files = argc - first;
    for (int i = 0; i < (files ? files : 1) && !pager.quit && !pager.failed && !pager.output.error; ++i)
    {
        const char* path = files ? argv[first + i] : "-";
        auto input = inputs.Open("more", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        // With terminal stdin, data and paging keys cannot be distinguished.
        // Copy until Ctrl+D instead, as for non-interactive output.
        bool paging = interactive && !(input == &inputs.standard && (standard.flags & HACKOS_TERMINAL));
        int ch;
        while (!pager.quit && !pager.failed && !pager.output.error && (ch = input->Next()) >= 0)
        {
            if (paging)
            {
                pager.Display(ch);
            }
            else
            {
                pager.output.Byte(ch);
                if (interactive)
                {
                    pager.output.Flush();
                }
            }
        }
        ok = inputs.Finish("more", path, input) && ok;
    }
    if (pager.failed)
    {
        FilterError("more", nullptr, "terminal input failed");
    }
    return pager.output.Finish("more") && ok && !pager.failed ? 0 : 1;
}
