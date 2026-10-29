#pragma once
#include "hackos/abi.h"

struct TerminalHistory
{
    char lines[HACKOS_TTY_HISTORY_ROWS][160];
    uint64_t next, epoch;
    unsigned count;

    void Clear()
    {
        for (auto& row : lines)
        {
            for (auto& ch : row)
            {
                ch = 0;
            }
        }
        next = count = 0;
        ++epoch;
    }

    void Push(const char* cells, unsigned columns)
    {
        auto& row = lines[next % HACKOS_TTY_HISTORY_ROWS];
        for (unsigned i = 0; i < 160; ++i)
        {
            row[i] = i < columns ? cells[i] : ' ';
        }
        ++next;
        if (count < HACKOS_TTY_HISTORY_ROWS)
        {
            ++count;
        }
    }

    void Snapshot(HackosTerminalView& view, const HackosTerminalState& state, const char* cells,
                  uint64_t requested) const
    {
        view.state = state;
        view.liveLine = next;
        view.oldestLine = next - count;
        view.firstLine = requested > next ? next : requested < view.oldestLine ? view.oldestLine : requested;
        view.epoch = epoch;
        for (auto& ch : view.cells)
        {
            ch = ' ';
        }
        for (unsigned row = 0; row < state.rows; ++row)
        {
            uint64_t line = view.firstLine + row;
            const char* from =
                line < next ? lines[line % HACKOS_TTY_HISTORY_ROWS] : cells + (line - next) * state.columns;
            for (unsigned column = 0; column < state.columns; ++column)
            {
                view.cells[row * state.columns + column] = from[column];
            }
        }
    }
};
