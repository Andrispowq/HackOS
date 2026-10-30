#include "support.hpp"
#include "hackos/runtime/terminal_damage.h"

static uint32_t Pixel(const TerminalViewport& view, const HackosTerminalSettings& settings, const char* label,
                      unsigned x, unsigned y)
{
    unsigned cw = settings.fontSize / 2, ch = settings.fontSize;
    unsigned column = x / cw, row = y / ch;
    if (row < view.image.state.rows && column < view.image.state.columns)
    {
        unsigned position = row * view.image.state.columns + column;
        uint32_t result = (uint8_t)view.image.cells[position] | ((uint32_t)view.styles[position] << 8);
        result ^= settings.fontColour ^ settings.bgColour;
        if (view.Selected(position))
        {
            result ^= 0xa5000000;
        }
        if (TerminalDamage::Caret(view) == position && x % cw < 2)
        {
            result ^= 0x5aff0000;
        }
        return result;
    }
    if (y >= view.image.state.rows * ch && y < view.image.state.rows * ch + 16)
    {
        return (uint8_t)label[0] + x % 7;
    }
    return settings.bgColour;
}

TEST(terminal_damage_spans_caret_status_and_retry)
{
    TerminalDamage damage = {};
    TerminalViewport view = {};
    view.Init();
    view.image.state.columns = 90;
    view.image.state.rows = 26;
    view.image.state.caret = 1;
    HackosTerminalSettings settings = {16, 0, 0xffffffff, 90, 26, 0};
    TerminalDamagePlan plan = {};
    damage.Plan(plan, view, settings, "Ready", 720, 432);
    CHECK(plan.full && plan.count == 1);
    damage.Commit(view, settings, "Ready", 720, 432);
    damage.Plan(plan, view, settings, "Ready", 720, 432);
    CHECK(!plan.count);
    view.image.cells[0] = 'a';
    view.image.state.x = 1;
    damage.Plan(plan, view, settings, "Ready", 720, 432);
    CHECK(!plan.full && plan.count == 1 && plan.rectangles[0].width == 16 && plan.rectangles[0].height == 16);
    // A failed upload/present does not call Commit; the retry must be identical.
    damage.Plan(plan, view, settings, "Ready", 720, 432);
    CHECK(plan.count == 1 && plan.rectangles[0].width == 16);
    damage.Commit(view, settings, "Ready", 720, 432);
    damage.Plan(plan, view, settings, "Copied", 720, 432);
    CHECK(plan.count == 1 && plan.rectangles[0].y == 416 && plan.rectangles[0].height == 16);
    settings.fontSize = 32;
    damage.Plan(plan, view, settings, "Copied", 720, 432);
    CHECK(plan.full);
    return 0;
}

TEST(terminal_damage_retained_matches_full_oracle)
{
    TerminalDamage damage = {};
    TerminalViewport view = {};
    view.Init();
    view.image.state.columns = 16;
    view.image.state.rows = 6;
    view.image.state.caret = 1;
    HackosTerminalSettings settings = {16, 0x123456ff, 0xabcdef00, 16, 6, 0};
    uint32_t* retained = (uint32_t*)test_alloc(400 * 240 * sizeof(uint32_t));
    CHECK(retained);
    test_fill(retained, 0x55, 400 * 240 * sizeof(uint32_t));
    uint32_t random = 0x63da;
    char label[2] = {'A', 0};
    for (unsigned step = 0; step < 500; ++step)
    {
        random = random * 1664525 + 1013904223;
        unsigned position = (random >> 16) % 96;
        switch (step % 10)
        {
        case 0:
            view.image.cells[position] = random;
            break;
        case 1:
            view.styles[position] = random & 8191;
            break;
        case 2:
            view.anchor = position;
            view.cursor = (position + 31) % 96;
            view.selected = !view.selected;
            break;
        case 3:
            view.image.state.x = position % 16;
            view.image.state.y = position / 16;
            break;
        case 4:
            view.image.state.caret = !view.image.state.caret;
            break;
        case 5:
            ++view.image.liveLine;
            break;
        case 6:
            view.image.firstLine = view.image.liveLine;
            label[0] = 'A' + step % 20;
            break;
        case 7:
            settings.fontSize = step % 3 == 0 ? 8 : step % 3 == 1 ? 16 : 32;
            break;
        case 8:
            settings.bgColour = random;
            break;
        case 9:
            view.image.cells[position] = ' ';
            view.styles[position] = 0;
            break;
        }
        unsigned width = 16 * settings.fontSize / 2 + (step / 100) % 3;
        unsigned height = 6 * settings.fontSize + 16 + (step / 100) % 3;
        TerminalDamagePlan plan = {};
        damage.Plan(plan, view, settings, label, width, height);
        CHECK(plan.count <= 61);
        for (unsigned i = 0; i < plan.count; ++i)
        {
            auto r = plan.rectangles[i];
            CHECK(r.x + r.width <= width && r.y + r.height <= height);
            for (unsigned y = r.y; y < r.y + r.height; ++y)
            {
                for (unsigned x = r.x; x < r.x + r.width; ++x)
                {
                    retained[y * 400 + x] = Pixel(view, settings, label, x, y);
                }
            }
        }
        for (unsigned y = 0; y < height; ++y)
        {
            for (unsigned x = 0; x < width; ++x)
            {
                CHECK(retained[y * 400 + x] == Pixel(view, settings, label, x, y));
            }
        }
        damage.Commit(view, settings, label, width, height);
    }
    test_free(retained);
    return 0;
}
