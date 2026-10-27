#pragma once
#include "hackos/types.h"

struct CompletionWord
{
    size_t start = 0;
    unsigned index = 0;
    char text[256] = {};
    char command[256] = {};
};

// Decode the final (possibly unfinished quoted) word without changing history.
inline bool CompletionPrefix(const char* line, CompletionWord& word)
{
    word = {};
    size_t used = 0;
    char quote = 0;
    bool active = false;
    for (size_t i = 0; line[i]; ++i)
    {
        char ch = line[i];
        if (!quote && (ch == '|' || ch == '<' || ch == '>'))
        {
            word.index = ch == '|' ? 0 : 1;
            word.command[0] = 0;
            word.text[0] = 0;
            word.start = i + 1;
            used = 0;
            active = false;
            continue;
        }

        if (!quote && (ch == ' ' || ch == '\t'))
        {
            if (active)
            {
                if (!word.index)
                {
                    for (size_t j = 0; j <= used; ++j)
                    {
                        word.command[j] = word.text[j];
                    }
                }

                ++word.index;
                used = 0;
                word.text[0] = 0;
                active = false;
            }

            word.start = i + 1;
            continue;
        }

        if (!active)
        {
            word.start = i;
            active = true;
        }

        if (ch == '\\')
        {
            if (!line[++i])
            {
                return false;
            }

            ch = line[i];
        }
        else if (ch == '\'' || ch == '"')
        {
            if (!quote)
            {
                quote = ch;
                continue;
            }

            if (quote == ch)
            {
                quote = 0;
                continue;
            }
        }

        if (used == sizeof(word.text) - 1)
        {
            return false;
        }

        word.text[used++] = ch;
        word.text[used] = 0;
    }

    return true;
}

inline char CompletionFold(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

inline bool CompletionMatch(const char* name, const char* prefix)
{
    while (*prefix)
    {
        if (CompletionFold(*name++) != CompletionFold(*prefix++))
        {
            return false;
        }
    }

    return true;
}

inline bool CompletionReplace(const char* line, size_t start, const char* word, char suffix, char (&output)[256])
{
    size_t used = 0;
    while (used < start && used < 255)
    {
        output[used] = line[used];
        ++used;
    }

    while (*word)
    {
        char ch = *word++;
        bool escape = ch == ' ' || ch == '\t' || ch == '\\' || ch == '\'' || ch == '"' || ch == '&' || ch == '|' ||
                      ch == '<' || ch == '>';
        if (used + (escape ? 2 : 1) > 255)
        {
            return false;
        }

        if (escape)
        {
            output[used++] = '\\';
        }

        output[used++] = ch;
    }

    if (suffix)
    {
        if (used == 255)
        {
            return false;
        }

        output[used++] = suffix;
    }

    output[used] = 0;
    return true;
}
