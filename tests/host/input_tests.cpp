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
    CHECK(line.Push((char)130) == LineBuffer::Ignored);
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

TEST(line_buffer_middle_editing_and_boundaries)
{
    LineBuffer line(true);
    line.Set("abcd");
    CHECK(line.Cursor() == 4);
    CHECK(line.Push((char)130) == LineBuffer::Moved);
    line.Push((char)130);
    CHECK(line.Cursor() == 2);
    CHECK(line.Push('X') == LineBuffer::Redraw);
    CHECK(test_name_equal(line.Text(), "abXcd") && line.Cursor() == 3);
    CHECK(line.Push((char)132) == LineBuffer::Redraw);
    CHECK(test_name_equal(line.Text(), "abXd"));
    CHECK(line.Push('\b') == LineBuffer::Redraw);
    CHECK(test_name_equal(line.Text(), "abd") && line.Cursor() == 2);
    line.SetCursor(0);
    CHECK(line.Push('\b') == LineBuffer::Ignored);
    line.Push((char)130);
    CHECK(line.Cursor() == 0);
    line.Push('z');
    CHECK(test_name_equal(line.Text(), "zabd") && line.Cursor() == 1);
    line.SetCursor(999);
    CHECK(line.Cursor() == line.Length());
    CHECK(line.Push((char)132) == LineBuffer::Ignored);
    line.Reset();
    CHECK(!line.Cursor() && !line.Length());
    for (unsigned i = 0; i < 255; ++i)
    {
        line.Push('a');
    }
    line.SetCursor(12);
    line.Push('b');
    CHECK(line.Overflowed() && line.Length() == 255);
    CHECK(line.Push('\n') == LineBuffer::Overflow);
    return 0;
}

#include "drivers/keyboard/gui_input.h"

TEST(gui_keys_modifiers_text_repeat_and_release)
{
    GuiKeyboardInput keyboard;
    GuiKey key;
    CHECK(keyboard.Push(0x2a, key) && key.down && key.modifiers == HACKOS_MOD_SHIFT);
    CHECK(keyboard.Push(0x1e, key) && key.key == HACKOS_PHYSICAL_A && key.text == 'A' && !key.flags);
    CHECK(keyboard.Push(0x1e, key) && key.flags == HACKOS_KEY_REPEAT && key.text == 'A');
    CHECK(keyboard.Push(0x9e, key) && !key.down && !key.text);
    CHECK(keyboard.Push(0xaa, key) && key.modifiers == 0);
    CHECK(keyboard.Push(0x1d, key) && key.modifiers == HACKOS_MOD_CTRL);
    CHECK(keyboard.Push(0x2e, key) && key.decoded == 3 && !key.text);
    CHECK(keyboard.Push(0xae, key) && !key.down);
    CHECK(keyboard.Push(0x9d, key) && !key.modifiers);
    CHECK(!keyboard.Push(0xe0, key));
    CHECK(keyboard.Push(0x38, key) && key.key == 0xb8 && key.modifiers == HACKOS_MOD_ALT);
    CHECK(keyboard.Push(0x1e, key) && !key.text);
    CHECK(!keyboard.Push(0xe0, key));
    CHECK(keyboard.Push(0xb8, key) && !key.modifiers);
    CHECK(keyboard.Push(0x9e, key) && !key.down);
    CHECK(keyboard.Push(0x1e, key) && key.text == 'a' && !key.flags);
    return 0;
}

TEST(gui_caps_special_sequences_and_bounds)
{
    GuiKeyboardInput keyboard;
    GuiKey key;
    CHECK(keyboard.Push(0x3a, key) && key.modifiers == HACKOS_MOD_CAPS);
    CHECK(keyboard.Push(0x3a, key) && key.modifiers == HACKOS_MOD_CAPS);
    CHECK(keyboard.Push(0xba, key));
    CHECK(keyboard.Push(0x1e, key) && key.text == 'A');
    const uint8_t ignored[] = {0xe0, 0x2a, 0xe0, 0x37, 0xe0, 0xb7, 0xe0, 0xaa, 0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5};
    for (auto byte : ignored)
    {
        CHECK(!keyboard.Push(byte, key));
    }
    CHECK(!keyboard.Push(0xe0, key));
    CHECK(keyboard.Push(0x4b, key) && key.key == 0xcb && !key.text);
    CHECK(!keyboard.Push(0xe0, key));
    CHECK(keyboard.Push(0xcb, key) && !key.down && key.key == 0xcb);
    for (unsigned i = 0; i < 4096; ++i)
    {
        if (keyboard.Push(i & 255, key))
        {
            CHECK(key.key < 256 && !(key.modifiers & ~15U));
            CHECK(!key.text || (key.down && key.text >= 32 && key.text <= 126));
        }
    }
    return 0;
}
