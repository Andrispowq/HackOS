#include "support.hpp"
#include "drivers/screen/framebuffer.h"

static Framebuffer make_fb(uint32_t* data, uint32_t w = 8, uint32_t h = 6)
{
    Framebuffer f;
    f.address = data;
    f.width = w;
    f.height = h;
    f.bpp = 32;
    f.pitch = w;
    return f;
}

TEST(framebuffer_pixel_bounds)
{
    uint32_t data[50] = {};
    auto f = make_fb(data + 1);
    f.PutPixel(7, 5, 0x12345678);
    CHECK(f.GetPixel(7, 5) == 0x12345678);
    f.PutPixel(8, 5, 1);
    f.PutPixel(7, 6, 1);
    CHECK(data[0] == 0 && data[49] == 0);
    CHECK(f.GetPixel(8, 0) == 0);
    return 0;
}

TEST(framebuffer_rectangle_inside)
{
    uint32_t data[48] = {};
    auto f = make_fb(data);
    f.DrawRect(2, 1, 3, 2, 99);
    for (unsigned y = 0; y < 6; ++y)
    {
        for (unsigned x = 0; x < 8; ++x)
        {
            CHECK(data[y * 8 + x] == ((x >= 2 && x < 5 && y >= 1 && y < 3) ? 99U : 0U));
        }
    }

    return 0;
}

TEST(framebuffer_clip_right_only)
{
    uint32_t data[48] = {};
    auto f = make_fb(data);
    f.DrawRect(7, 1, 4, 2, 99);
    for (unsigned y = 0; y < 6; ++y)
    {
        for (unsigned x = 0; x < 8; ++x)
        {
            CHECK(data[y * 8 + x] == ((x == 7 && y >= 1 && y < 3) ? 99U : 0U));
        }
    }

    return 0;
}

TEST(framebuffer_clip_bottom_only)
{
    uint32_t data[48] = {};
    auto f = make_fb(data);
    f.DrawRect(1, 5, 2, 4, 99);
    for (unsigned y = 0; y < 6; ++y)
    {
        for (unsigned x = 0; x < 8; ++x)
        {
            CHECK(data[y * 8 + x] == ((y == 5 && x >= 1 && x < 3) ? 99U : 0U));
        }
    }

    return 0;
}

TEST(framebuffer_padded_pitch)
{
    uint32_t data[60] = {};
    auto f = make_fb(data);
    f.pitch = 10;
    f.PutPixel(1, 1, 99);
    CHECK(data[11] == 99);
    CHECK(data[9] == 0);
    return 0;
}

TEST(framebuffer_clear)
{
    uint32_t data[48] = {};
    auto f = make_fb(data);
    f.Clear(0xabcdef12);
    for (auto pixel : data)
    {
        CHECK(pixel == 0xabcdef12);
    }

    return 0;
}

TEST(framebuffer_cursor_restores_background)
{
    uint32_t data[48];
    for (unsigned i = 0; i < 48; ++i)
    {
        data[i] = i;
    }

    auto f = make_fb(data);
    uint8_t cursor[32];
    test_fill(cursor, 0xff, 32);
    f.DrawMouseCursor(cursor, 6, 4, 99);
    CHECK(f.GetPixel(6, 4) == 99);
    f.ClearMouseCursor(cursor, 6, 4);
    for (unsigned i = 0; i < 48; ++i)
    {
        CHECK(data[i] == i);
    }

    return 0;
}

TEST(framebuffer_cursor_preserves_new_drawing)
{
    uint32_t data[48] = {};
    auto f = make_fb(data);
    uint8_t cursor[32];
    test_fill(cursor, 0xff, 32);
    f.DrawMouseCursor(cursor, 6, 4, 99);
    f.PutPixel(6, 4, 42);
    f.ClearMouseCursor(cursor, 6, 4);
    CHECK(f.GetPixel(6, 4) == 42);
    return 0;
}

TEST(framebuffer_copy_clips_and_preserves_pitch_padding)
{
    uint32_t source[12] = {1, 2, 3, 99, 4, 5, 6, 99, 7, 8, 9, 99};
    uint32_t destination[22] = {};
    auto from = make_fb(source, 3, 3);
    from.pitch = 4;
    auto to = make_fb(destination + 1, 4, 3);
    to.pitch = 6;
    to.CopyFrom(from, 2, 1);

    for (unsigned i = 0; i < 22; i++)
    {
        uint32_t expected = 0;
        if (i == 9)
        {
            expected = 1;
        }

        if (i == 10)
        {
            expected = 2;
        }

        if (i == 15)
        {
            expected = 4;
        }

        if (i == 16)
        {
            expected = 5;
        }

        CHECK(destination[i] == expected);
    }

    to.CopyFrom(from, 0xffffffff, 0);
    to.CopyFrom(from, 0, 0xffffffff);
    CHECK(destination[0] == 0 && destination[21] == 0);
    return 0;
}

TEST(framebuffer_region_clips_both_buffers)
{
    uint32_t source[12] = {1, 2, 3, 99, 4, 5, 6, 99, 7, 8, 9, 99};
    uint32_t destination[22] = {};
    auto from = make_fb(source, 3, 3);
    from.pitch = 4;
    auto to = make_fb(destination + 1, 4, 3);
    to.pitch = 6;
    to.CopyRegion(from, 2, 1, 0xffffffff, 0xffffffff);
    for (unsigned i = 0; i < 22; ++i)
    {
        CHECK(destination[i] == (i == 9 ? 6U : i == 15 ? 9U : 0U));
    }

    to.CopyRegion(from, 0xffffffff, 0, 1, 1);
    to.CopyRegion(from, 0, 0xffffffff, 1, 1);
    CHECK(destination[0] == 0 && destination[21] == 0);
    return 0;
}

TEST(framebuffer_changed_spans_and_no_device_reads)
{
    uint32_t source[3 * 104] = {};
    uint32_t saved[3 * 105] = {};
    uint32_t device[3 * 106 + 2];
    test_fill(device, 0xaa, sizeof(device));
    auto from = make_fb(source, 99, 3);
    from.pitch = 104;
    auto previous = make_fb(saved, 99, 3);
    previous.pitch = 105;
    auto to = make_fb(device + 1, 99, 3);
    to.pitch = 106;
    // Device contents deliberately differ: unchanged RAM must cause no writes.
    CHECK(to.PresentChanged(from, previous, false) == 0);
    CHECK(device[1] == 0xaaaaaaaa);
    source[104 + 1] = 7;
    source[104 + 65] = 8;
    source[104 + 98] = 9;
    CHECK(to.PresentChanged(from, previous, false) == 67);
    for (unsigned y = 0; y < 3; ++y)
    {
        for (unsigned x = 0; x < 106; ++x)
        {
            bool changed = y == 1 && (x < 32 || (x >= 64 && x < 99));
            CHECK(device[1 + y * 106 + x] == (changed ? source[y * 104 + x] : 0xaaaaaaaa));
        }
    }

    CHECK(to.PresentChanged(from, previous, false) == 0);
    CHECK(to.PresentChanged(from, previous, true) == 99 * 3);
    for (unsigned y = 0; y < 3; ++y)
    {
        for (unsigned x = 0; x < 99; ++x)
        {
            CHECK(device[1 + y * 106 + x] == source[y * 104 + x]);
            CHECK(saved[y * 105 + x] == source[y * 104 + x]);
        }

        CHECK(device[1 + y * 106 + 99] == 0xaaaaaaaa);
        CHECK(saved[y * 105 + 99] == 0);
    }

    CHECK(device[0] == 0xaaaaaaaa && device[319] == 0xaaaaaaaa);
    return 0;
}

TEST(framebuffer_present_matches_full_copy_sequence)
{
    uint32_t source[70 * 8] = {};
    uint32_t saved[70 * 8] = {};
    uint32_t device[70 * 8] = {};
    auto from = make_fb(source, 70, 8);
    auto previous = make_fb(saved, 70, 8);
    auto to = make_fb(device, 70, 8);
    for (unsigned frame = 0; frame < 100; ++frame)
    {
        from.Clear(frame / 20);
        from.DrawRect(frame % 70, frame % 8, 13, 3, frame + 1);
        to.PresentChanged(from, previous, frame == 0);
        CHECK(test_equal(source, device, sizeof(source)));
        CHECK(test_equal(source, saved, sizeof(source)));
    }

    return 0;
}

TEST(framebuffer_damage_union_clips_without_overflow)
{
    FramebufferDamage damage;
    CHECK(damage.Empty());
    damage.Include(100, 20, 1, 1, 100, 60);
    damage.Include(1, 1, 0, 20, 100, 60);
    CHECK(damage.Empty());
    damage.Include(80, 50, 0xffffffff, 0xffffffff, 100, 60);
    CHECK(damage.left == 80 && damage.top == 50 && damage.right == 100 && damage.bottom == 60);
    damage.Include(5, 7, 2, 3, 100, 60);
    CHECK(damage.left == 5 && damage.top == 7 && damage.right == 100 && damage.bottom == 60);
    damage.Include(0, 0, 100, 60, 100, 60);
    CHECK(damage.left == 0 && damage.top == 0 && damage.right == 100 && damage.bottom == 60);
    damage = {};
    CHECK(damage.Empty());
    return 0;
}

TEST(framebuffer_damage_view_preserves_outside_pixels)
{
    uint32_t source[80 * 5] = {};
    uint32_t saved[80 * 5] = {};
    uint32_t device[80 * 5] = {};
    // A narrow region starts at an unaligned pixel and retains the full pitch.
    auto from = make_fb(source + 83, 35, 2);
    auto previous = make_fb(saved + 83, 35, 2);
    auto to = make_fb(device + 83, 35, 2);
    from.pitch = previous.pitch = to.pitch = 80;
    from.Clear(42);
    source[0] = 99;
    CHECK(to.PresentChanged(from, previous, false) == 70);
    for (unsigned i = 0; i < 80 * 5; ++i)
    {
        bool inside = (i >= 83 && i < 118) || (i >= 163 && i < 198);
        CHECK(device[i] == (inside ? 42U : 0U));
        CHECK(saved[i] == device[i]);
    }

    return 0;
}

TEST(framebuffer_clip_preserves_coordinates_and_padding)
{
    uint32_t source[11 * 8], pixels[13 * 8 + 2];
    for (unsigned i = 0; i < 11 * 8; ++i)
    {
        source[i] = i + 1;
    }
    for (auto& pixel : pixels)
    {
        pixel = 0xabcdef;
    }

    auto from = make_fb(source, 9, 8);
    from.pitch = 11;
    auto to = make_fb(pixels + 1, 10, 8);
    to.pitch = 13;
    to.SetClip({4, 3, 8, 7});
    to.Clear(0);
    to.CopyFrom(from, 2, 1);
    to.PutPixel(0, 0, 99);
    to.DrawRect(0xffffffff, 2, 20, 20, 77);
    for (unsigned y = 0; y < 8; ++y)
    {
        for (unsigned x = 0; x < 13; ++x)
        {
            bool inside = x >= 4 && x < 8 && y >= 3 && y < 7;
            CHECK(pixels[1 + y * 13 + x] == (inside ? source[(y - 1) * 11 + x - 2] : 0xabcdef));
        }
    }
    CHECK(pixels[0] == 0xabcdef && pixels[105] == 0xabcdef);
    to.SetClip({100, 100, 200, 200});
    to.Clear(33);
    CHECK(pixels[1] == 0xabcdef);
    to.ClearClip();
    to.PutPixel(0, 0, 99);
    CHECK(pixels[1] == 99);
    return 0;
}

TEST(framebuffer_changed_bounds_handles_pitch_and_tail)
{
    uint32_t current[70 * 4] = {}, saved[73 * 4] = {};
    auto from = make_fb(current, 67, 4), previous = make_fb(saved, 67, 4);
    from.pitch = 70;
    previous.pitch = 73;
    CHECK(from.ChangedBounds(previous).Empty());
    from.PutPixel(66, 3, 1);
    auto damage = from.ChangedBounds(previous);
    CHECK(damage.left == 64 && damage.right == 67 && damage.top == 3 && damage.bottom == 4);
    from.PutPixel(2, 1, 1);
    damage = from.ChangedBounds(previous);
    CHECK(damage.left == 0 && damage.right == 67 && damage.top == 1 && damage.bottom == 4);
    previous.width = 1;
    damage = from.ChangedBounds(previous);
    CHECK(damage.left == 0 && damage.right == 67 && damage.top == 0 && damage.bottom == 4);
    return 0;
}

TEST(framebuffer_damage_list_merge_and_overflow)
{
    FramebufferDamageList list;
    list.Include({1, 1, 3, 3});
    list.Include({5, 1, 7, 3});
    CHECK(list.count == 2);
    list.Include({3, 1, 5, 3});
    CHECK(list.count == 1 && list.regions[0].left == 1 && list.regions[0].right == 7);
    list = {};
    for (unsigned i = 0; i < 17; ++i)
    {
        list.Include({i * 3, 1, i * 3 + 1, 2});
    }
    CHECK(list.count == 1 && list.regions[0].left == 0 && list.regions[0].right == 49);
    list.Include({});
    CHECK(list.count == 1);
    return 0;
}

TEST(framebuffer_clipped_scene_matches_full_redraw)
{
    uint32_t retained[83 * 40] = {}, reference[83 * 40] = {}, client[23 * 12];
    auto partial = make_fb(retained, 80, 40), full = make_fb(reference, 80, 40), window = make_fb(client, 20, 12);
    partial.pitch = full.pitch = 83;
    window.pitch = 23;
    for (unsigned i = 0; i < 23 * 12; ++i)
    {
        client[i] = i;
    }
    auto draw = [&](Framebuffer& target, unsigned x, unsigned y)
    {
        target.Clear(1);
        target.CopyFrom(window, 9, 5);
        target.DrawRect(x, y, 25, 14, 42);
        target.DrawRect(40, 8, 10, 12, 99);
    };
    draw(partial, 0, 0);
    unsigned oldX = 0, oldY = 0;
    for (unsigned frame = 1; frame < 80; ++frame)
    {
        unsigned x = frame % 60, y = frame % 30;
        FramebufferDamageList damage;
        FramebufferDamage oldRegion, newRegion;
        oldRegion.Include(oldX, oldY, 25, 14, 80, 40);
        newRegion.Include(x, y, 25, 14, 80, 40);
        damage.Include(oldRegion);
        damage.Include(newRegion);
        for (unsigned i = 0; i < damage.count; ++i)
        {
            partial.SetClip(damage.regions[i]);
            draw(partial, x, y);
        }
        partial.ClearClip();
        draw(full, x, y);
        CHECK(test_equal(retained, reference, sizeof(retained)));
        oldX = x;
        oldY = y;
    }
    return 0;
}

TEST(framebuffer_random_clipping_matches_scalar_oracle)
{
    uint32_t actual[96 * 32 + 8], expected[96 * 32 + 8], source[80 * 24 + 8];
    uint32_t random = 0x6c202610;
    auto next = [&]()
    {
        random = random * 1664525 + 1013904223;
        return random;
    };
    for (unsigned scene = 0; scene < 512; ++scene)
    {
        unsigned width = 1 + next() % 79, height = 1 + next() % 29;
        unsigned pitch = width + next() % 8, offset = 1 + next() % 3;
        unsigned sw = 1 + next() % 67, sh = 1 + next() % 23, sp = sw + next() % 8;
        auto target = make_fb(actual + offset, width, height), input = make_fb(source + 1, sw, sh);
        target.pitch = pitch;
        input.pitch = sp;
        for (unsigned i = 0; i < 96 * 32 + 8; ++i)
        {
            actual[i] = expected[i] = 0xa5c39e71;
        }
        for (unsigned i = 0; i < 80 * 24 + 8; ++i)
        {
            source[i] = next();
        }
        for (unsigned step = 0; step < 8; ++step)
        {
            unsigned cx = next() % 90, cy = next() % 35;
            unsigned right = cx + next() % 90, bottom = cy + next() % 35;
            if (step == 7)
            {
                right = bottom = 0xffffffff;
            }
            target.SetClip({cx, cy, right, bottom});
            unsigned x = next() % 90, y = next() % 35;
            unsigned w = step == 6 ? 0xffffffff : next() % 90;
            unsigned h = step == 6 ? 0xffffffff : next() % 35;
            if (step == 5)
            {
                x = 0xffffffff;
            }
            uint32_t colour = next();
            bool copy = step & 1;
            if (copy)
            {
                target.CopyFrom(input, x, y);
                w = sw;
                h = sh;
            }
            else
            {
                target.DrawRect(x, y, w, h, colour);
            }
            for (unsigned row = 0; row < height; ++row)
            {
                for (unsigned column = 0; column < width; ++column)
                {
                    if (column >= cx && column < right && row >= cy && row < bottom && column >= x && row >= y &&
                        (uint64_t)column - x < w && (uint64_t)row - y < h)
                    {
                        expected[offset + row * pitch + column] =
                            copy ? source[1 + (row - y) * sp + column - x] : colour;
                    }
                }
            }
            CHECK(test_equal(actual, expected, sizeof(actual)));
        }
    }
    return 0;
}

TEST(framebuffer_presentation_width_stride_and_colour_matrix)
{
    uint32_t current[96 * 5], saved[97 * 5], device[98 * 5];
    unsigned widths[] = {1, 2, 3, 4, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65};
    uint32_t colours[] = {0, 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff, 0x12345678};
    for (unsigned width : widths)
    {
        for (unsigned padding = 0; padding < 8; ++padding)
        {
            auto source = make_fb(current + 1, width, 4), previous = make_fb(saved + 2, width, 4),
                 output = make_fb(device + 3, width, 4);
            source.pitch = width + padding;
            previous.pitch = width + padding + 1;
            output.pitch = width + padding + 2;
            for (auto& pixel : saved)
            {
                pixel = 0x87654321;
            }
            for (auto& pixel : device)
            {
                pixel = 0x87654321;
            }
            for (uint32_t colour : colours)
            {
                source.Clear(colour);
                CHECK(output.PresentChanged(source, previous, true) == width * 4);
                CHECK(output.PresentChanged(source, previous, false) == 0);
                source.PutPixel(width - 1, 3, colour ^ 0x00abcdef);
                CHECK(output.PresentChanged(source, previous, false) > 0);
                for (unsigned row = 0; row < 4; ++row)
                {
                    for (unsigned column = 0; column < width; ++column)
                    {
                        CHECK(output.GetPixel(column, row) == source.GetPixel(column, row));
                        CHECK(previous.GetPixel(column, row) == source.GetPixel(column, row));
                    }
                    for (unsigned column = width; column < output.pitch; ++column)
                    {
                        CHECK(device[3 + row * output.pitch + column] == 0x87654321);
                    }
                }
                CHECK(device[2] == 0x87654321 && device[3 + 4 * output.pitch] == 0x87654321);
            }
        }
    }
    return 0;
}
