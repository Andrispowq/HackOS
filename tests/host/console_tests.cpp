#include "support.hpp"
#include "drivers/screen/console.h"

TEST(console_settings_atomic_validation_and_reset)
{
    Console console;
    CHECK(console.Configure(HACKOS_TERMINAL_RESET, 0, 1919, 1079));
    auto initial = console.Settings();
    CHECK(initial.fontSize == 30 && initial.bgColour == 0x000000ff && initial.fontColour == 0xffffffff);
    CHECK(initial.columns == 127 && initial.rows == 35 && !initial.reserved);
    console.current_x = 12;
    console.current_y = 34;
    const uint64_t invalidSizes[] = {0, 7, 9, 65, 0x100000010ULL, ~0ULL};
    for (auto size : invalidSizes)
    {
        CHECK(!console.Configure(HACKOS_TERMINAL_FONT_SIZE, size, 1919, 1079));
    }
    CHECK(!console.Configure(HACKOS_TERMINAL_RESET, 1, 1919, 1079));
    CHECK(!console.Configure(4, 0, 1919, 1079));
    CHECK(!console.Configure(HACKOS_TERMINAL_BG_COLOUR, 0x100000000ULL, 1919, 1079));
    CHECK(!console.Configure(HACKOS_TERMINAL_FONT_SIZE, 64, 31, 63));
    auto unchanged = console.Settings();
    CHECK(test_equal(&initial, &unchanged, sizeof(initial)));
    CHECK(console.current_x == 12 && console.current_y == 34);
    for (unsigned size = 8; size <= 64; size += 2)
    {
        CHECK(console.Configure(HACKOS_TERMINAL_FONT_SIZE, size, 1919, 1079));
        auto current = console.Settings();
        CHECK(current.fontSize == size && current.columns == 1919 / (size / 2) && current.rows == 1079 / size);
        CHECK(!console.current_x && !console.current_y);
    }
    CHECK(console.Configure(HACKOS_TERMINAL_RESET, 0, 1919, 1079));
    unchanged = console.Settings();
    CHECK(test_equal(&initial, &unchanged, sizeof(initial)));
    return 0;
}

TEST(console_rgba_roundtrip_and_alpha)
{
    Console console;
    CHECK(console.Configure(HACKOS_TERMINAL_BG_COLOUR, 0x80402080, 800, 600));
    CHECK(console.Configure(HACKOS_TERMINAL_FONT_COLOUR, 0xffffff80, 800, 600));
    CHECK(console.Settings().bgColour == 0x80402080 && console.Settings().fontColour == 0xffffff80);
    CHECK(console.Background() == 0xff402010 && console.Foreground() == 0xffa09088);
    CHECK(console.Configure(HACKOS_TERMINAL_FONT_COLOUR, 0xffffff00, 800, 600));
    CHECK(console.Foreground() == console.Background());
    CHECK(console.Configure(HACKOS_TERMINAL_BG_COLOUR, 0xffffff00, 800, 600));
    CHECK(console.Background() == 0xff000000);
    CHECK(console.Configure(HACKOS_TERMINAL_RESET, 0, 800, 600));
    CHECK(console.Background() == 0xff000000 && console.Foreground() == 0xffffffff);
    return 0;
}

TEST(console_glyph_scaling_clipping_unsigned_and_padding)
{
    uint8_t glyphs[256 * 16] = {};
    // A diagonal and its mirror distinguish glyph rows, bits and signed bytes.
    for (unsigned i = 0; i < 16; ++i)
    {
        glyphs[255 * 16 + i] = (0x80 >> (i % 8)) | (1 << (i % 8));
    }
    PSF1_HEADER header = {{PSF1_MAGIC0, PSF1_MAGIC1}, 0, 16};
    PSF1_FONT font = {&header, glyphs};
    uint32_t pixels[68 * 36 + 2];
    Framebuffer fb;
    fb.address = pixels + 1;
    fb.width = 34;
    fb.height = 68;
    fb.pitch = 36;
    fb.bpp = 32;
    for (unsigned height = 8; height <= 64; height += 2)
    {
        unsigned width = height / 2;
        const unsigned offsets[] = {0, 30};
        for (unsigned offset : offsets)
        {
            test_fill(pixels, 0, sizeof(pixels));
            DrawConsoleGlyph(fb, font, 255, offset, offset, width, height, 99);
            for (unsigned y = 0; y < fb.height; ++y)
            {
                for (unsigned x = 0; x < fb.pitch; ++x)
                {
                    uint32_t expected = 0;
                    if (x < fb.width && x >= offset && y >= offset && x - offset < width && y - offset < height)
                    {
                        unsigned sourceY = (y - offset) * 16 / height;
                        unsigned sourceX = (x - offset) * 8 / width;
                        if (glyphs[255 * 16 + sourceY] & (0x80 >> sourceX))
                        {
                            expected = 99;
                        }
                    }
                    CHECK(pixels[1 + y * fb.pitch + x] == expected);
                }
            }
            CHECK(!pixels[0] && !pixels[68 * 36 + 1]);
        }
    }
    test_fill(pixels, 0, sizeof(pixels));
    DrawConsoleGlyph(fb, font, 255, 0xffffffff, 0, 32, 64, 99);
    DrawConsoleGlyph(fb, font, 255, 0, 0xffffffff, 32, 64, 99);
    DrawConsoleGlyph(fb, font, 255, 0, 0, 0, 64, 99);
    for (auto pixel : pixels)
    {
        CHECK(pixel == 0);
    }
    return 0;
}

TEST(console_scroll_uses_whole_rows_and_clears_remainder)
{
    uint32_t pixels[6 * 21] = {};
    Framebuffer fb;
    fb.address = pixels;
    fb.width = 4;
    fb.height = 21;
    fb.pitch = 6;
    fb.bpp = 32;
    Console console;
    CHECK(console.Configure(HACKOS_TERMINAL_FONT_SIZE, 8, 4, 21));
    for (unsigned y = 0; y < fb.height; ++y)
    {
        fb.DrawRect(0, y, 4, 1, y + 1);
    }
    console.Scroll(fb);
    for (unsigned y = 0; y < fb.height; ++y)
    {
        for (unsigned x = 0; x < fb.pitch; ++x)
        {
            CHECK(pixels[y * fb.pitch + x] == (x >= 4 ? 0 : y < 8 ? y + 9 : 0xff000000));
        }
    }
    fb.height = 8;
    console.Scroll(fb);
    CHECK(fb.GetPixel(0, 0) == 0xff000000 && fb.GetPixel(3, 7) == 0xff000000);
    return 0;
}

TEST(console_glyph_respects_damage_clip)
{
    uint8_t glyphs[256 * 16];
    test_fill(glyphs, 0xff, sizeof(glyphs));
    PSF1_HEADER header = {{PSF1_MAGIC0, PSF1_MAGIC1}, 0, 16};
    PSF1_FONT font = {&header, glyphs};
    uint32_t pixels[20 * 20] = {};
    Framebuffer fb = {pixels, 18, 20, 32, 20};
    fb.SetClip({5, 7, 9, 11});
    DrawConsoleGlyph(fb, font, 'A', 3, 4, 8, 16, 99);
    for (unsigned y = 0; y < 20; ++y)
    {
        for (unsigned x = 0; x < 20; ++x)
        {
            CHECK(pixels[y * 20 + x] == (x >= 5 && x < 9 && y >= 7 && y < 11 ? 99U : 0U));
        }
    }
    return 0;
}
