#include "files.h"
#include "hackos/runtime/text_buffer.h"

static TextBuffer document;
static uint64_t topRow;
static const unsigned columns = 72, rows = 20;

static void Draw(const char* path, const char* message)
{
    uint64_t cursorRow = 0;
    unsigned column = 0;
    for (uint64_t i = 0; i < document.cursor; ++i)
    {
        if (document.text[i] == '\n' || ++column == columns)
        {
            ++cursorRow;
            column = 0;
        }
    }

    if (cursorRow < topRow)
    {
        topRow = cursorRow;
    }
    else if (cursorRow >= topRow + rows)
    {
        topRow = cursorRow - rows + 1;
    }

    syscall_console(1);
    Print("edit: ");
    // Keep long paths from pushing the viewport off a small console.
    syscall_user_write(path, Length(path) > 60 ? 60 : Length(path));
    Print(document.dirty ? " *\n" : "\n");
    Print("Ctrl+S save | Ctrl+X exit (unsaved changes discarded)\n");
    Print("Arrows move | Backspace/Delete edit | | marks cursor, > marks tab\n");
    Print(message);
    Print("\n");

    uint64_t position = 0, row = 0;
    while (row < topRow + rows)
    {
        char line[columns + 3];
        unsigned used = 0;
        column = 0;
        bool newline = false;
        while (column < columns)
        {
            if (position == document.cursor)
            {
                line[used++] = '|';
            }

            if (position == document.length)
            {
                break;
            }

            char ch = document.text[position++];
            if (ch == '\n')
            {
                newline = true;
                break;
            }

            line[used++] = ch == '\t' ? '>' : ch;
            ++column;
        }

        if (row >= topRow)
        {
            line[used++] = '\n';
            syscall_user_write(line, used);
        }

        if (position == document.length && column < columns && !newline)
        {
            break;
        }

        ++row;
    }
}

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "edit <file>"))
    {
        return 2;
    }

    // A background editor must fail before clearing or consuming the terminal.
    if (syscall_console(2) == HACKOS_DENIED)
    {
        Print("edit: foreground terminal required.\n");
        return 1;
    }

    uint64_t status = LoadWholeFile(argv[1], document.text, document.length);
    bool create = status == HACKOS_NOT_FOUND;
    if ((status && !create) || !document.Valid())
    {
        Print("edit: cannot load file (limit 32768 bytes; ASCII, LF and tabs only).\n");
        return 1;
    }

    const char* message = create ? "New file. Not saved yet." : "File loaded.";
    Draw(argv[1], message);
    for (;;)
    {
        uint64_t key = syscall_input();
        if (!key)
        {
            syscall_yield();
            continue;
        }

        if (key == HACKOS_DENIED || key == HACKOS_ERROR)
        {
            Print("edit: terminal unavailable.\n");
            return 1;
        }

        if (key == HACKOS_KEY_EXIT)
        {
            syscall_console(1);
            Print(document.dirty ? "edit: exited; unsaved changes discarded.\n" : "edit: exited.\n");
            return 0;
        }

        if (key == HACKOS_KEY_SAVE)
        {
            if (syscall_file_save(argv[1], document.text, document.length, create ? HACKOS_SAVE_CREATE : 0))
            {
                message = "Save failed. Buffer kept; check path, space, open files or disk errors.";
            }
            else
            {
                create = false;
                document.dirty = false;
                message = "Saved.";
            }
        }
        else if (key == HACKOS_INPUT_OVERFLOW)
        {
            message = "Input overflow: some keys lost. Check text before saving.";
        }
        else
        {
            message = document.Key(key) ? "" : "File is full (32768 bytes).";
        }

        Draw(argv[1], message);
    }
}
