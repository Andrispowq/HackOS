#pragma once

// Canonical FAT/VFS paths use ~ as root and uppercase components. Input and
// output must not alias. No heap allocation, traversal above root, or truncation.
inline bool NormalizePath(const char* input, const char* cwd, char (&output)[256])
{
    if (!input || !*input) return false;

    unsigned length = 1;
    output[0] = '~';
    output[1] = 0;
    bool absolute = input[0] == '/' || input[0] == '~';
    if (input[0] == '~' && input[1] && input[1] != '/') return false;

    if (!absolute)
    {
        for (unsigned i = 1; cwd[i]; i++)
        {
            if (length == 255) return false;
            output[length++] = cwd[i];
        }
    }
    else if (*input == '~')
    {
        input++;
    }

    while (*input)
    {
        while (*input == '/')
        {
            input++;
        }
        if (!*input) break;

        const char* start = input;
        unsigned count = 0;
        while (*input && *input != '/')
        {
            unsigned char ch = *input++;
            if (ch < 32 || ch >= 127 || ch == '\\' || ch == '~') return false;
            count++;
        }

        if (count == 1 && start[0] == '.') continue;
        if (count == 2 && start[0] == '.' && start[1] == '.')
        {
            while (length > 1 && output[length - 1] != '/')
            {
                length--;
            }
            if (length > 1) length--;
            continue;
        }

        if (length + count + 1 > 255) return false;
        output[length++] = '/';
        for (unsigned i = 0; i < count; i++)
        {
            char ch = start[i];
            output[length++] = ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
        }
    }

    output[length] = 0;
    return true;
}
