#pragma once
#include "command.h"

inline void FilterError(const char* command, const char* path, const char* message)
{
    Error(command);
    Error(": ");
    if (path)
    {
        Error(path);
        Error(": ");
    }
    Error(message);
    Error("\n");
}

inline bool Decimal(const char* text, uint64_t& result)
{
    result = 0;
    if (!*text)
    {
        return false;
    }
    for (; *text; ++text)
    {
        if (*text < '0' || *text > '9' || result > (~0ULL - (*text - '0')) / 10)
        {
            return false;
        }
        result = result * 10 + *text - '0';
    }
    return true;
}

inline bool Space(unsigned char ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

class FilterOutput
{
  public:
    uint64_t error = 0;

    bool Flush()
    {
        unsigned offset = 0;
        while (!error && offset < used)
        {
            uint64_t written = syscall_fd_write(1, buffer + offset, used - offset, HACKOS_WAIT_FOREVER);
            if (!written || written > used - offset)
            {
                error = written ? written : HACKOS_ERROR;
                break;
            }
            offset += written;
        }
        used = 0;
        return !error;
    }

    void Byte(char ch)
    {
        if (error)
        {
            return;
        }
        buffer[used++] = ch;
        if (used == sizeof(buffer))
        {
            Flush();
        }
    }

    void Bytes(const char* data, uint64_t size)
    {
        for (uint64_t i = 0; i < size && !error; ++i)
        {
            Byte(data[i]);
        }
    }

    void Text(const char* text)
    {
        Bytes(text, Length(text));
    }

    void Number(uint64_t value)
    {
        char digits[20];
        unsigned count = 0;
        do
        {
            digits[count++] = '0' + value % 10;
            value /= 10;
        } while (value);
        while (count)
        {
            Byte(digits[--count]);
        }
    }

    bool Finish(const char* command)
    {
        Flush();
        if (error && error != HACKOS_BROKEN_PIPE)
        {
            FilterError(command, nullptr, "write failed");
        }
        return !error;
    }

  private:
    char buffer[HACKOS_IO_LIMIT];
    unsigned used = 0;
};

class FilterInput
{
  public:
    uint64_t fd = 0;
    bool failed = false;

    bool Open(const char* command, const char* path)
    {
        fd = syscall_fd_open(path, HACKOS_OPEN_READ);
        position = length = 0;
        failed = false;
        if (fd >= HACKOS_FD_LIMIT)
        {
            FilterError(command, path, "cannot open");
            return false;
        }
        return true;
    }

    // A byte (including NUL), -1 for EOF, -2 for a read failure.
    int Next()
    {
        if (position == length)
        {
            uint64_t count = syscall_fd_read(fd, buffer, sizeof(buffer), HACKOS_WAIT_FOREVER);
            if (count > sizeof(buffer))
            {
                failed = true;
                return -2;
            }
            if (!count)
            {
                return -1;
            }
            position = 0;
            length = count;
        }
        return buffer[position++];
    }

    bool Finish(const char* command, const char* path, bool close)
    {
        bool ok = !failed;
        if (failed)
        {
            FilterError(command, path, "read failed");
        }
        if (close && syscall_fd_close(fd))
        {
            FilterError(command, path, "close failed");
            ok = false;
        }
        return ok;
    }

  private:
    unsigned char buffer[HACKOS_IO_LIMIT];
    unsigned position = 0, length = 0;
};

// Keep stdin's read-ahead across repeated '-' operands (notably head).
class FilterInputs
{
  public:
    FilterInput standard, file;

    FilterInput* Open(const char* command, const char* path)
    {
        if (Equal(path, "-"))
        {
            return &standard;
        }
        return file.Open(command, path) ? &file : nullptr;
    }

    bool Finish(const char* command, const char* path, FilterInput* input)
    {
        return input->Finish(command, path, input == &file);
    }
};

inline int FilterUsage(const char* usage, bool help = false)
{
    if (help)
    {
        return Write(1, usage, Length(usage)) ? 0 : 1;
    }
    Error(usage);
    return 2;
}
