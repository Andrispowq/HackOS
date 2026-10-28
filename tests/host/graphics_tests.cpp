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
