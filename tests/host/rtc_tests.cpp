#include "support.hpp"
#include "arch/x86_64/timer/rtc_sample.h"

TEST(rtc_binary_bcd_and_12_hour)
{
    tm value = {};
    CHECK(DecodeRTC({0x07, 0x08, 0x09, 0x02, 0x01, 0x26, 0x20, 2}, value));
    CHECK(value.year == 126 && value.month == 0 && value.day == 2 && value.hour == 9 && value.minute == 8 &&
          value.second == 7);
    CHECK(DecodeRTC({59, 58, 23, 31, 12, 26, 20, 6}, value));
    CHECK(value.month == 11 && value.hour == 23 && value.minute == 58 && value.second == 59);
    CHECK(DecodeRTC({0, 0, 0x12, 1, 1, 0x26, 0, 0}, value) && value.hour == 0);
    CHECK(DecodeRTC({0, 0, 0x92, 1, 1, 0x26, 0, 0}, value) && value.hour == 12);
    CHECK(DecodeRTC({0, 0, 0x89, 1, 1, 26, 20, 4}, value) && value.hour == 21);
    CHECK(DecodeRTC({0, 0, 0, 1, 1, 0, 0x21, 2}, value) && value.year == 200);
    return 0;
}

TEST(rtc_rejects_invalid_calendar)
{
    tm value = {};
    CHECK(DecodeRTC({0, 0, 0, 29, 2, 0, 20, 6}, value));
    CHECK(!DecodeRTC({0, 0, 0, 29, 2, 0, 21, 6}, value));
    CHECK(!DecodeRTC({0, 0, 0, 29, 2, 26, 20, 6}, value));
    CHECK(!DecodeRTC({0, 0, 0, 31, 4, 26, 20, 6}, value));
    CHECK(!DecodeRTC({0, 0, 0, 1, 0, 26, 20, 6}, value));
    CHECK(!DecodeRTC({0, 0, 24, 1, 1, 26, 20, 6}, value));
    CHECK(!DecodeRTC({0, 0, 0, 1, 1, 26, 20, 4}, value));
    CHECK(!DecodeRTC({0, 0x6a, 0, 1, 1, 0x26, 0x20, 2}, value));
    CHECK(!DecodeRTC({60, 0, 0, 1, 1, 26, 20, 6}, value));
    return 0;
}

TEST(rtc_rollover_and_bounded_update)
{
    unsigned reads = 0;
    tm value = {};
    CHECK(!SampleRTC(
        [&](uint8_t) -> uint8_t
        {
            ++reads;
            return 0x80;
        },
        0, value));
    CHECK(reads == 1024);
    unsigned samples = 0;
    CHECK(SampleRTC(
        [&](uint8_t index) -> uint8_t
        {
            if (index == 0)
            {
                ++samples;
                return samples == 1 ? 59 : 0;
            }
            if (index == 2)
            {
                return samples == 1 ? 59 : 0;
            }
            if (index == 4)
            {
                return samples == 1 ? 23 : 0;
            }
            if (index == 7)
            {
                return samples == 1 ? 31 : 1;
            }
            if (index == 8)
            {
                return samples == 1 ? 12 : 1;
            }
            if (index == 9)
            {
                return samples == 1 ? 26 : 27;
            }
            if (index == 0x32)
            {
                return 20;
            }
            if (index == 0x0b)
            {
                return 6;
            }
            return 0;
        },
        0x32, value));
    CHECK(samples == 3 && value.year == 127 && value.month == 0 && value.day == 1 && value.hour == 0);
    return 0;
}
