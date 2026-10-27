#pragma once
#include "hackos/abi.h"

struct ShellRedirect
{
    unsigned target = 0;
    int source = -1;
    uint64_t mode = 0;
    char* path = nullptr;
};

struct ShellStage
{
    unsigned argc = 0, redirectCount = 0;
    char* argv[HACKOS_MAX_ARGS + 1] = {};
    ShellRedirect redirects[8] = {};
};

struct ShellPipeline
{
    unsigned count = 0;
    bool background = false;
    ShellStage stages[4] = {};
    char words[512] = {};
};

inline bool ShellOperator(char ch)
{
    return ch == '|' || ch == '<' || ch == '>' || ch == '&';
}

inline void ShellSpace(const char*& input)
{
    while (*input == ' ' || *input == '\t')
    {
        ++input;
    }
}

inline bool ShellWord(const char*& input, char*& output, char* end)
{
    bool started = false;
    char quote = 0;
    while (*input)
    {
        char ch = *input;
        if (!quote && (ch == ' ' || ch == '\t' || ShellOperator(ch)))
        {
            break;
        }

        started = true;
        ++input;
        if (ch == '\\')
        {
            if (!*input)
            {
                return false;
            }

            ch = *input++;
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

        if (output + 1 >= end)
        {
            return false;
        }

        *output++ = ch;
    }

    if (!started || quote || output == end)
    {
        return false;
    }

    *output++ = 0;
    return true;
}

// Parse from immutable input so adjacent operators cannot overwrite unread text.
// Quoted/escaped operators remain ordinary bytes. Redirections preserve order.
inline bool ParsePipeline(const char* input, ShellPipeline& pipeline)
{
    pipeline = {};
    ShellSpace(input);
    if (!*input)
    {
        return true;
    }

    pipeline.count = 1;
    char* output = pipeline.words;
    char* end = output + sizeof(pipeline.words);
    for (;;)
    {
        ShellSpace(input);
        auto& stage = pipeline.stages[pipeline.count - 1];
        if (!*input)
        {
            return stage.argc != 0;
        }

        if (*input == '|')
        {
            if (!stage.argc || pipeline.count == 4)
            {
                return false;
            }

            ++input;
            ++pipeline.count;
            continue;
        }

        if (*input == '&')
        {
            ++input;
            ShellSpace(input);
            pipeline.background = true;
            return stage.argc && !*input;
        }

        int target = -1;
        if (input[0] >= '0' && input[0] <= '2' && (input[1] == '<' || input[1] == '>'))
        {
            target = *input++ - '0';
        }

        if (*input == '<' || *input == '>')
        {
            if (stage.redirectCount == 8)
            {
                return false;
            }

            auto& redirect = stage.redirects[stage.redirectCount++];
            bool read = *input++ == '<';
            redirect.target = target >= 0 ? target : read ? 0 : 1;
            redirect.mode = read ? HACKOS_OPEN_READ : HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_TRUNCATE;
            if (!read && *input == '>')
            {
                ++input;
                redirect.mode = HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_APPEND;
            }

            if (*input == '&')
            {
                ++input;
                if (*input < '0' || *input > '2')
                {
                    return false;
                }

                redirect.source = *input++ - '0';
                if (*input && *input != ' ' && *input != '\t' && !ShellOperator(*input))
                {
                    return false;
                }
            }
            else
            {
                ShellSpace(input);
                redirect.path = output;
                if (!ShellWord(input, output, end) || !*redirect.path)
                {
                    return false;
                }
            }

            continue;
        }

        if (stage.argc == HACKOS_MAX_ARGS + 1)
        {
            return false;
        }

        stage.argv[stage.argc++] = output;
        if (!ShellWord(input, output, end))
        {
            return false;
        }
    }
}
