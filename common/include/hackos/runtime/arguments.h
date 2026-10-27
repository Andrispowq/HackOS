#pragma once

// In-place command argument parsing. Quotes group words, backslash escapes the
// next character, and a bare trailing & requests background execution.
// Returns -1 for syntax errors and -2 when argv capacity is exceeded.
inline int ParseArguments(char* input, char** argv, unsigned capacity, bool& background)
{
    char* read = input;
    char* write = input;
    unsigned count = 0;
    background = false;
    for (;;)
    {
        while (*read == ' ' || *read == '\t')
        {
            ++read;
        }

        if (!*read)
        {
            return count;
        }

        char* word = write;
        char quote = 0;
        bool bare = true;
        while (*read)
        {
            char ch = *read;
            if (!quote && (ch == ' ' || ch == '\t'))
            {
                break;
            }

            ++read;
            if (ch == '\\')
            {
                bare = false;
                if (!*read)
                {
                    return -1;
                }

                *write++ = *read++;
            }
            else if (ch == '\'' || ch == '"')
            {
                bare = false;
                if (!quote)
                {
                    quote = ch;
                }
                else if (quote == ch)
                {
                    quote = 0;
                }
                else
                {
                    *write++ = ch;
                }
            }
            else
            {
                *write++ = ch;
            }
        }

        if (quote)
        {
            return -1;
        }

        if (*read)
        {
            ++read; // advance before overwriting the delimiter
        }

        *write++ = 0;
        if (bare && word[0] == '&' && !word[1])
        {
            while (*read == ' ' || *read == '\t')
            {
                ++read;
            }

            if (*read || !count)
            {
                return -1;
            }

            background = true;
            return count;
        }

        if (count == capacity)
        {
            return -2;
        }

        argv[count++] = word;
    }
}
