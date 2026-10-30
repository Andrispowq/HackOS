#pragma once

namespace Hackos
{
// ASCII host names only; one optional final root dot is removed.
inline bool NormalizeHostname(const char* input, char (&output)[256])
{
    unsigned length = 0;
    while (length < 256 && input[length])
    {
        ++length;
    }
    if (!length || length == 256) return false;
    if (input[length - 1] == '.') --length;
    if (!length || length > 253) return false;
    unsigned label = 0;
    bool letters = false;
    for (unsigned i = 0; i < length; ++i)
    {
        char c = input[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '.')
        {
            if (!label || input[i - 1] == '-') return false;
            label = 0;
        }
        else
        {
            bool alpha = c >= 'a' && c <= 'z';
            if (!alpha && !(c >= '0' && c <= '9') && c != '-') return false;
            if ((!label && c == '-') || ++label > 63) return false;
            letters = letters || alpha;
        }
        output[i] = c;
    }
    if (!label || input[length - 1] == '-' || !letters) return false;
    output[length] = 0;
    return true;
}
}
