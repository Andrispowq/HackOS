#pragma once
#include "hackos/abi.h"

// Userspace-only viewport and selection; no pointers into kernel-owned storage.
struct TerminalViewport
{
    HackosTerminalView image;
    uint16_t styles[HACKOS_TTY_CELLS];
    uint64_t requested;
    unsigned anchor, cursor, buttons;
    bool ready, selected, dragging;

    void Init()
    {
        requested = HACKOS_TTY_LIVE;
        ready = selected = dragging = false;
        anchor = cursor = buttons = 0;
    }

    bool Accept(const HackosTerminalView& next, const uint16_t* attributes = nullptr)
    {
        if (ready && image.epoch == next.epoch && selected)
        {
            return false; // A selection owns the exact displayed snapshot until dismissed.
        }
        if (ready && image.epoch != next.epoch)
        {
            selected = dragging = false;
        }
        // Avoid compiler-emitted hosted memcpy for this large freestanding record.
        auto* destination = reinterpret_cast<unsigned char*>(&image);
        const auto* source = reinterpret_cast<const unsigned char*>(&next);
        for (unsigned i = 0; i < sizeof(image); ++i)
        {
            destination[i] = source[i];
        }
        for (unsigned i = 0; i < HACKOS_TTY_CELLS; ++i)
        {
            styles[i] = attributes ? attributes[i] : 0;
        }
        ready = true;
        if (requested != HACKOS_TTY_LIVE)
        {
            requested = image.firstLine;
        }
        return true;
    }

    void Live()
    {
        requested = HACKOS_TTY_LIVE;
        selected = dragging = false;
    }

    void Scroll(int64_t rows)
    {
        if (!ready)
        {
            return;
        }
        selected = dragging = false;
        uint64_t first = requested == HACKOS_TTY_LIVE ? image.liveLine : requested;
        if (first < image.oldestLine)
        {
            first = image.oldestLine;
        }
        if (rows > 0)
        {
            uint64_t count = rows;
            first = count > first - image.oldestLine ? image.oldestLine : first - count;
        }
        else
        {
            uint64_t count = (uint64_t)(-(rows + 1)) + 1;
            first = count > image.liveLine - first ? image.liveLine : first + count;
        }
        requested = first == image.liveLine ? HACKOS_TTY_LIVE : first;
    }

    bool Mouse(int x, int y, unsigned pressed, unsigned width = 8, unsigned height = 16)
    {
        bool changed = false;
        if (!ready)
        {
            buttons = pressed;
            return false;
        }
        bool inside =
            x >= 0 && y >= 0 && (unsigned)x < image.state.columns * width && (unsigned)y < image.state.rows * height;
        if ((pressed & 1) && !(buttons & 1) && inside)
        {
            anchor = cursor = (y / height) * image.state.columns + x / width;
            selected = dragging = true;
            requested = image.firstLine;
            changed = true;
        }
        if (dragging)
        {
            unsigned column = x < 0 ? 0 : (unsigned)x / width;
            unsigned row = y < 0 ? 0 : (unsigned)y / height;
            if (column >= image.state.columns)
            {
                column = image.state.columns - 1;
            }
            if (row >= image.state.rows)
            {
                row = image.state.rows - 1;
            }
            unsigned position = row * image.state.columns + column;
            changed |= cursor != position;
            cursor = position;
            if (!(pressed & 1))
            {
                dragging = false;
            }
        }
        buttons = pressed;
        return changed;
    }

    void ResetInput(unsigned held)
    {
        dragging = false;
        buttons = held;
    }

    bool Selected(unsigned position) const
    {
        unsigned first = anchor < cursor ? anchor : cursor, last = anchor > cursor ? anchor : cursor;
        return selected && position >= first && position <= last;
    }

    uint64_t Copy(char* output, unsigned capacity) const
    {
        if (!ready || !selected)
        {
            return HACKOS_ERROR;
        }
        unsigned first = anchor < cursor ? anchor : cursor, last = anchor > cursor ? anchor : cursor;
        unsigned length = 0, columns = image.state.columns;
        for (unsigned row = first / columns; row <= last / columns; ++row)
        {
            unsigned begin = row == first / columns ? first : row * columns;
            unsigned end = row == last / columns ? last + 1 : (row + 1) * columns;
            while (end > begin && image.cells[end - 1] == ' ')
            {
                --end;
            }
            for (unsigned i = begin; i < end; ++i)
            {
                if (length == capacity)
                {
                    return HACKOS_BUFFER_SMALL;
                }
                output[length++] = image.cells[i];
            }
            if (row != last / columns)
            {
                if (length == capacity)
                {
                    return HACKOS_BUFFER_SMALL;
                }
                output[length++] = '\n';
            }
        }
        return length;
    }
};
