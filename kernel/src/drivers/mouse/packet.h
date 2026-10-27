#pragma once

#include "hackos/types.h"

class MousePacketDecoder
{
  public:
    void Configure(uint8_t device)
    {
        wheel = device == 3;
        index = 0;
    }

    bool Push(uint8_t value)
    {
        if (!index && (!(value & 8) || value == 0xfa || value == 0xfe))
        {
            return false;
        }

        bytes[index++] = value;
        if (index != (wheel ? 4 : 3))
        {
            return false;
        }

        index = 0;
        return true;
    }

    int32_t Wheel() const
    {
        // IntelliMouse ID 3 sends a signed fourth byte; positive is down.
        return wheel ? (int8_t)bytes[3] : 0;
    }

    uint8_t bytes[4] = {};

  private:
    unsigned index = 0;
    bool wheel = false;
};
