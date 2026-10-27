#pragma once
#include "command.h"

// Plain text survives redirection without terminal escape sequences. Stop on
// the first failed write so utilities also report broken pipes/disk errors.
class ReportOutput
{
  public:
    bool ok = true;

    void Text(const char* value)
    {
        if (ok)
        {
            ok = Write(1, value, Length(value));
        }
    }

    void Number(uint64_t value, unsigned base = 10, unsigned width = 0)
    {
        char buffer[65];
        unsigned pos = sizeof(buffer);
        buffer[--pos] = 0;
        do
        {
            buffer[--pos] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value);
        while (sizeof(buffer) - pos - 1 < width && pos)
        {
            buffer[--pos] = '0';
        }
        Text(buffer + pos);
    }

    void Field(const char* label)
    {
        Text("  ");
        Text(label);
        for (unsigned i = Length(label); i < 23; ++i)
        {
            Text(" ");
        }
        Text(" : ");
    }

    void Heading(const char* label)
    {
        Text("\n");
        Text(label);
        Text("\n");
        for (unsigned i = 0; i < Length(label); ++i)
        {
            Text("-");
        }
        Text("\n");
    }

    void Bytes(const char* label, uint64_t bytes)
    {
        Field(label);
        Number(bytes / 1048576);
        Text(".");
        Number((bytes % 1048576) * 100 / 1048576, 10, 2);
        Text(" MiB (");
        Number(bytes);
        Text(" bytes)\n");
    }
};
