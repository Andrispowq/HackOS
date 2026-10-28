#pragma once
#include "input.h"
#include "hackos/abi.h"

struct GuiKey
{
    uint32_t key, decoded, text, modifiers, flags;
    bool down;
};

// The legacy translator remains the terminal contract. This decoder adds a
// bounded physical-key state, without allocation or routing in IRQ context.
class GuiKeyboardInput
{
    KeyboardInput translated;
    bool extended = false, caps = false;
    uint8_t pauseBytes = 0;
    uint64_t held[4] = {};

    bool Held(unsigned key) const
    {
        return held[key / 64] & (1ULL << (key % 64));
    }

  public:
    uint32_t Modifiers() const
    {
        return ((Held(0x2a) || Held(0x36)) ? HACKOS_MOD_SHIFT : 0) |
               ((Held(0x1d) || Held(0x9d)) ? HACKOS_MOD_CTRL : 0) | ((Held(0x38) || Held(0xb8)) ? HACKOS_MOD_ALT : 0) |
               (caps ? HACKOS_MOD_CAPS : 0);
    }

    bool Push(uint8_t byte, GuiKey& result)
    {
        translated.PushScancode(byte);
        translated.TakeInterrupt();
        char decoded = 0;
        translated.Pop(decoded);
        if (pauseBytes)
        {
            --pauseBytes;
            return false;
        }
        if (byte == 0xe1)
        {
            pauseBytes = 5;
            extended = false;
            return false;
        }
        if (byte == 0xe0)
        {
            extended = true;
            return false;
        }
        bool down = !(byte & 0x80);
        uint32_t base = byte & 0x7f;
        uint32_t key = base | (extended ? 0x80 : 0);
        bool wasExtended = extended;
        extended = false;
        // Ignore malformed/unsupported codes, including Print Screen's fake shifts.
        if (!base || base > 0x58 ||
            (wasExtended && base != 0x1c && base != 0x1d && base != 0x35 && base != 0x38 && base != 0x47 &&
             base != 0x48 && base != 0x49 && base != 0x4b && base != 0x4d && base != 0x4f && base != 0x50 &&
             base != 0x51 && base != 0x52 && base != 0x53))
        {
            return false;
        }
        bool repeat = down && Held(key);
        if (down)
        {
            held[key / 64] |= 1ULL << (key % 64);
        }
        else
        {
            held[key / 64] &= ~(1ULL << (key % 64));
        }
        if (key == 0x3a && down && !repeat)
        {
            caps = !caps;
        }
        uint32_t modifiers = Modifiers();
        uint32_t text = down && !(modifiers & (HACKOS_MOD_CTRL | HACKOS_MOD_ALT)) && (uint8_t)decoded >= 32 &&
                                (uint8_t)decoded <= 126
                            ? (uint8_t)decoded
                            : 0;
        result = {key, (uint8_t)decoded, text, modifiers, repeat ? HACKOS_KEY_REPEAT : 0, down};
        return true;
    }
};
