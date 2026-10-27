#include "support.hpp"
#include "drivers/keyboard/input.h"
#include "shell/line_buffer.h"
#include "arch/x86_64/interrupts/idt.h"

TEST(keyboard_make_break_and_unknown_codes)
{
    KeyboardInput keys;
    char c;
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'a');
    keys.PushScancode(0x9e);
    CHECK(!keys.Pop(c));
    for (unsigned code = 0x80; code < 0xe0; ++code)
    {
        keys.PushScancode(code);
    }

    for (unsigned code = 0x59; code < 0x80; ++code)
    {
        keys.PushScancode(code);
    }

    CHECK(!keys.Pop(c));
    return 0;
}

TEST(keyboard_shift_caps_and_punctuation)
{
    KeyboardInput keys;
    char c;
    keys.PushScancode(0x2a);
    keys.PushScancode(0x36);
    keys.PushScancode(0xaa);
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'A');
    keys.PushScancode(0x06);
    CHECK(keys.Pop(c) && c == '%');
    keys.PushScancode(0xb6);
    keys.PushScancode(0x3a);
    keys.PushScancode(0x3a); // typematic must not toggle twice
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'A');
    keys.PushScancode(0x2a);
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'a');
    keys.PushScancode(0xaa);
    keys.PushScancode(0xba);
    keys.PushScancode(0x3a);
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'a');
    return 0;
}

TEST(keyboard_extended_and_pause_sequences)
{
    KeyboardInput keys;
    char c;
    const uint8_t ignored[] = {0xe0, 0x2a, 0xe0, 0x37, 0xe0, 0xb7, 0xe0, 0xaa, 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5};
    for (auto code : ignored)
    {
        keys.PushScancode(code);
    }

    CHECK(!keys.Pop(c));
    keys.PushScancode(0x1e);
    CHECK(keys.Pop(c) && c == 'a');
    keys.PushScancode(0xe0);
    keys.PushScancode(0x1c);
    CHECK(keys.Pop(c) && c == '\n');
    keys.PushScancode(0xe0);
    keys.PushScancode(0x35);
    CHECK(keys.Pop(c) && c == '/');
    return 0;
}

TEST(keyboard_fifo_wrap_and_overflow_recovery)
{
    KeyboardInput keys;
    char c;
    for (int pass = 0; pass < 4; ++pass)
    {
        for (int i = 0; i < 255; ++i)
        {
            keys.PushScancode(i % 2 ? 0x30 : 0x1e);
        }

        for (int i = 0; i < 255; ++i)
        {
            CHECK(keys.Pop(c) && c == (i % 2 ? 'b' : 'a'));
        }

        CHECK(!keys.Pop(c));
    }

    for (int i = 0; i < 256; ++i)
    {
        keys.PushScancode(0x1e);
    }

    CHECK(keys.DiscardOverflow());
    CHECK(!keys.Pop(c));
    CHECK(!keys.DiscardOverflow());
    keys.PushScancode(0x1c);
    CHECK(keys.Pop(c) && c == '\n');
    return 0;
}

TEST(keyboard_all_byte_values_are_bounded)
{
    KeyboardInput keys;
    char c;
    for (int pass = 0; pass < 10; ++pass)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            keys.PushScancode(i);
            while (keys.Pop(c))
            {
                CHECK(c != 0);
            }
        }
    }

    return 0;
}

TEST(line_buffer_edits_and_submits_only_on_enter)
{
    LineBuffer line;
    CHECK(line.Push('\b') == LineBuffer::Ignored);
    CHECK(line.Push('a') == LineBuffer::Echo);
    CHECK(line.Push('b') == LineBuffer::Echo);
    CHECK(line.Push('\b') == LineBuffer::Erase);
    CHECK(line.Push('%') == LineBuffer::Echo);
    CHECK(line.Push('\t') == LineBuffer::Ignored);
    CHECK(line.Push('\n') == LineBuffer::Submit);
    CHECK(test_name_equal(line.Text(), "a%"));
    line.Reset();
    CHECK(line.Push('\n') == LineBuffer::Submit);
    CHECK(line.Text()[0] == 0);
    return 0;
}

TEST(line_buffer_rejects_entire_overlong_command)
{
    LineBuffer line;
    for (int i = 0; i < 255; ++i)
    {
        CHECK(line.Push('x') == LineBuffer::Echo);
    }

    CHECK(line.Text()[255] == 0);
    CHECK(line.Push('\n') == LineBuffer::Submit);
    CHECK(line.Push('y') == LineBuffer::Ignored);
    CHECK(line.Push('\b') == LineBuffer::Ignored);
    CHECK(line.Push('\n') == LineBuffer::Overflow);
    line.Reset();
    CHECK(line.Push('z') == LineBuffer::Echo);
    CHECK(test_name_equal(line.Text(), "z"));
    return 0;
}

TEST(interrupt_frame_layout)
{
    CHECK(sizeof(Registers) == 704 && alignof(Registers) == 16);
    CHECK(__builtin_offsetof(Registers, rax) == 576);
    CHECK(__builtin_offsetof(Registers, rsp) == 688);
    return 0;
}

TEST(keyboard_arrows_control_and_full_queue_interrupt)
{
    KeyboardInput keys;
    char c;
    keys.PushScancode(0xe0);
    keys.PushScancode(0x48);
    CHECK(keys.Pop(c) && (uint8_t)c == 128);
    keys.PushScancode(0xe0);
    keys.PushScancode(0xc8);
    CHECK(!keys.Pop(c));
    keys.PushScancode(0xe0);
    keys.PushScancode(0x50);
    CHECK(keys.Pop(c) && (uint8_t)c == 129);
    for (int i = 0; i < 260; ++i)
    {
        keys.PushScancode(0x1e);
    }

    keys.PushScancode(0x1d);
    keys.PushScancode(0x2e);
    CHECK(keys.TakeInterrupt() && !keys.TakeInterrupt());
    CHECK(!keys.DiscardOverflow());
    CHECK(keys.Pop(c) && c == 3 && !keys.Pop(c));
    keys.PushScancode(0x9d);
    keys.PushScancode(0x2e);
    CHECK(keys.Pop(c) && c == 'c');
    keys.PushScancode(0xe0);
    keys.PushScancode(0x1d);
    keys.PushScancode(0x2a);
    keys.PushScancode(0x2e);
    CHECK(keys.TakeInterrupt() && keys.Pop(c) && c == 3);
    keys.PushScancode(0xe0);
    keys.PushScancode(0x9d);
    keys.PushScancode(0x2e);
    CHECK(keys.Pop(c) && c == 'C');
    return 0;
}

TEST(keyboard_editor_controls_left_right_ctrl_and_breaks)
{
    KeyboardInput keys;
    char c;
    for (unsigned side = 0; side < 2; ++side)
    {
        if (side)
        {
            keys.PushScancode(0xe0);
        }
        keys.PushScancode(0x1d);
        keys.PushScancode(0x1f);
        CHECK(keys.Pop(c) && c == 19);
        keys.PushScancode(0x9f);
        CHECK(!keys.Pop(c));
        keys.PushScancode(0x2d);
        CHECK(keys.Pop(c) && c == 24);
        keys.PushScancode(0x2a); // Shift does not suppress Ctrl-Z.
        keys.PushScancode(0x2c);
        CHECK(keys.Pop(c) && c == 26);
        keys.PushScancode(0xac);
        keys.PushScancode(0xaa);
        CHECK(!keys.Pop(c));
        if (side)
        {
            keys.PushScancode(0xe0);
        }
        keys.PushScancode(0x9d);
        keys.PushScancode(0x1f);
        CHECK(keys.Pop(c) && c == 's');
    }

    const uint8_t scans[] = {0x4b, 0x4d, 0x53};
    for (unsigned i = 0; i < 3; ++i)
    {
        keys.PushScancode(0xe0);
        keys.PushScancode(scans[i]);
        CHECK(keys.Pop(c) && (uint8_t)c == 130 + i);
        keys.PushScancode(0xe0);
        keys.PushScancode(scans[i] | 0x80);
        CHECK(!keys.Pop(c));
    }

    return 0;
}
