#pragma once
#include "hackos/runtime/terminal_view.h"

struct TerminalDamageRect
{
    unsigned x, y, width, height;
};

struct TerminalDamagePlan
{
    TerminalDamageRect rectangles[61];
    unsigned count;
    bool full;
};

// Cache only the last successfully presented image. Failed/cancelled frames must
// not advance it, so a retry still covers every changed pixel.
struct TerminalDamage
{
    TerminalViewport painted;
    HackosTerminalSettings appearance;
    unsigned width, height;
    char status[160];
    bool valid;

    static unsigned Caret(const TerminalViewport& view)
    {
        const auto& state = view.image.state;
        unsigned position = state.y * state.columns + state.x;
        if (!state.caret || view.selected || view.image.firstLine != view.image.liveLine || position < state.caretBack)
        {
            return HACKOS_TTY_CELLS;
        }
        return position - state.caretBack;
    }

    static bool SameText(const char* left, const char* right)
    {
        for (unsigned i = 0; i < 160; ++i)
        {
            if (left[i] != right[i])
            {
                return false;
            }
            if (!left[i])
            {
                return true;
            }
        }
        return true;
    }

    void Plan(TerminalDamagePlan& plan, const TerminalViewport& view, const HackosTerminalSettings& settings,
              const char* label, unsigned w, unsigned h) const
    {
        plan.count = 0;
        const auto& state = view.image.state;
        plan.full = !valid || w != width || h != height || state.columns != painted.image.state.columns ||
                    state.rows != painted.image.state.rows || settings.fontSize != appearance.fontSize ||
                    settings.bgColour != appearance.bgColour || settings.fontColour != appearance.fontColour;
        if (plan.full)
        {
            plan.rectangles[plan.count++] = {0, 0, w, h};
            return;
        }
        unsigned oldCaret = Caret(painted), caret = Caret(view);
        unsigned cellWidth = settings.fontSize / 2, cellHeight = settings.fontSize;
        for (unsigned row = 0; row < state.rows; ++row)
        {
            unsigned first = state.columns, end = 0;
            for (unsigned col = 0; col < state.columns; ++col)
            {
                unsigned position = row * state.columns + col;
                bool changed = view.image.cells[position] != painted.image.cells[position] ||
                               view.styles[position] != painted.styles[position] ||
                               view.Selected(position) != painted.Selected(position) ||
                               (oldCaret != caret && (position == oldCaret || position == caret));
                if (changed)
                {
                    if (first == state.columns)
                    {
                        first = col;
                    }
                    end = col + 1;
                }
            }
            unsigned x = first * cellWidth, y = row * cellHeight;
            if (end && x < w && y < h)
            {
                unsigned right = end * cellWidth < w ? end * cellWidth : w;
                unsigned rows = cellHeight < h - y ? cellHeight : h - y;
                plan.rectangles[plan.count++] = {x, y, right - x, rows};
            }
        }
        unsigned y = state.rows * cellHeight;
        if (!SameText(status, label) && y < h)
        {
            plan.rectangles[plan.count++] = {0, y, w, h - y < 16 ? h - y : 16};
        }
    }

    void Commit(const TerminalViewport& view, const HackosTerminalSettings& settings, const char* label, unsigned w,
                unsigned h)
    {
        auto* destination = reinterpret_cast<unsigned char*>(&painted);
        const auto* source = reinterpret_cast<const unsigned char*>(&view);
        for (unsigned i = 0; i < sizeof(painted); ++i)
        {
            destination[i] = source[i];
        }
        appearance = settings;
        bool ended = false;
        for (unsigned i = 0; i < sizeof(status); ++i)
        {
            if (!ended && !label[i])
            {
                ended = true;
            }
            status[i] = ended ? 0 : label[i];
        }
        width = w;
        height = h;
        valid = true;
    }
};
