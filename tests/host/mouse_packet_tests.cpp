#include "support.hpp"
#include "drivers/mouse/packet.h"

TEST(mouse_packets_basic_and_wheel)
{
    MousePacketDecoder decoder;
    decoder.Configure(0);
    CHECK(!decoder.Push(0xfa));
    CHECK(!decoder.Push(0xfe));
    CHECK(!decoder.Push(0));
    CHECK(!decoder.Push(8));
    CHECK(!decoder.Push(0xfa));
    CHECK(decoder.Push(0xfe));
    CHECK(decoder.bytes[1] == 0xfa && decoder.bytes[2] == 0xfe);
    CHECK(decoder.Wheel() == 0);

    decoder.Configure(3);
    for (int delta = -127; delta <= 127; delta++)
    {
        CHECK(!decoder.Push(8));
        CHECK(!decoder.Push(0));
        CHECK(!decoder.Push(0));
        CHECK(decoder.Push((uint8_t)delta));
        CHECK(decoder.Wheel() == delta);
    }

    CHECK(!decoder.Push(8));
    decoder.Configure(0);
    CHECK(!decoder.Push(8));
    CHECK(!decoder.Push(1));
    CHECK(decoder.Push(2));
    CHECK(decoder.Wheel() == 0);
    return 0;
}
