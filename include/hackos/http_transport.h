#pragma once
#include "hackos/user.h"
#include "hackos/stdlib.h"
#include "hackos/tls.h"
#include "hackos/runtime/http.h"

namespace Hackos::Http
{
class Transport
{
    void* tls = nullptr;
    bool eof = false;

    static bool Entropy(uint8_t* seed)
    {
        uint32_t a = 1, b, c, d;
        asm volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        if (!(c & (1u << 30)))
        {
            return false;
        }
        for (unsigned i = 0; i < 32; i += 4)
        {
            uint32_t value = 0;
            unsigned char good = 0;
            for (unsigned attempt = 0; attempt < 16; ++attempt)
            {
                asm volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(good));
                if (good && value && value != 0xffffffffu)
                {
                    break;
                }
                good = 0;
            }
            if (!good)
            {
                return false;
            }
            for (unsigned n = 0; n < 4; ++n)
            {
                seed[i + n] = value >> (8 * n);
            }
        }
        return true;
    }

    bool Failure(const char* message)
    {
        error = message;
        return false;
    }

  public:
    const char* error = nullptr;
    unsigned tlsError = 0;
    uint64_t transferred = 0;

    ~Transport()
    {
        Reset();
    }

    void Reset()
    {
        if (tls)
        {
            volatile uint8_t* bytes = (volatile uint8_t*)tls;
            for (size_t i = 0; i < hackos_tls_size(); ++i)
            {
                bytes[i] = 0;
            }
            free(tls);
        }
        tls = nullptr;
        error = nullptr;
        tlsError = 0;
        transferred = 0;
        eof = false;
    }

    bool Start(const Url& url)
    {
        Reset();
        if (!url.secure)
        {
            return true;
        }
        HackosClock clock = {};
        if (syscall_clock(&clock) || clock.year < 2020 || clock.year > 9999 || !clock.month || clock.month > 12 ||
            !clock.day || clock.hour > 23 || clock.minute > 59 || clock.second > 59)
        {
            return Failure("TLS: invalid RTC date/time.");
        }
        unsigned year = clock.year;
        bool leap = !(year % 4) && (year % 100 || !(year % 400));
        const unsigned months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (clock.day > months[clock.month - 1] + (clock.month == 2 && leap))
        {
            return Failure("TLS: invalid RTC date/time.");
        }
        uint32_t days = 365 * year + (year + 3) / 4 - (year + 99) / 100 + (year + 399) / 400;
        for (unsigned m = 1; m < clock.month; ++m)
        {
            days += months[m - 1] + (m == 2 && leap);
        }
        days += clock.day - 1;
        uint8_t seed[32] = {}, ip[4];
        bool entropy = Entropy(seed);
        if (entropy)
        {
            tls = malloc(hackos_tls_size());
        }
        int good = tls && hackos_tls_start(tls, url.host, ParseIPv4(url.host, ip) ? ip : nullptr, days,
                                           clock.hour * 3600 + clock.minute * 60 + clock.second, seed);
        volatile uint8_t* wipe = seed;
        for (unsigned i = 0; i < sizeof(seed); ++i)
        {
            wipe[i] = 0;
        }
        if (!entropy)
        {
            return Failure("TLS: secure entropy unavailable.");
        }
        if (!good)
        {
            return Failure("TLS: cannot allocate or initialize TLS session.");
        }
        return true;
    }

    unsigned Events(unsigned desired) const
    {
        if (!tls)
        {
            return desired;
        }
        unsigned state = hackos_tls_state(tls);
        return (state & TLS_SEND_RECORD ? HACKOS_POLL_WRITE : 0) | (state & TLS_RECV_RECORD ? HACKOS_POLL_READ : 0);
    }

    bool Pump(uint64_t socket)
    {
        if (!tls)
        {
            return true;
        }
        // At most eight TCP records per GUI turn; no waits inside this method.
        for (unsigned i = 0; i < 8; ++i)
        {
            unsigned state = hackos_tls_state(tls);
            if (state & TLS_CLOSED)
            {
                tlsError = hackos_tls_error(tls);
                if (tlsError)
                {
                    return Failure(hackos_tls_error_message(tlsError));
                }
                eof = true;
                return true;
            }
            unsigned operation = state & TLS_SEND_RECORD   ? TLS_SEND_RECORD
                                 : state & TLS_RECV_RECORD ? TLS_RECV_RECORD
                                                           : 0;
            if (!operation)
            {
                return true;
            }
            size_t capacity;
            uint8_t* buffer = hackos_tls_buffer(tls, operation, &capacity);
            if (!buffer || !capacity)
            {
                return true;
            }
            HackosTcpStream record = {};
            record.size = capacity < sizeof(record.data) ? capacity : sizeof(record.data);
            if (operation == TLS_SEND_RECORD)
            {
                for (unsigned j = 0; j < record.size; ++j)
                {
                    record.data[j] = buffer[j];
                }
            }
            auto result =
                syscall_tcp_stream(operation == TLS_SEND_RECORD ? HACKOS_TCP_WRITE : HACKOS_TCP_READ, socket, &record);
            if (result == HACKOS_RETRY)
            {
                return true;
            }
            if (result)
            {
                return Failure("TLS: TCP connection failed.");
            }
            if (!record.size)
            {
                return Failure("TLS: truncated connection (missing close_notify).");
            }
            if (operation == TLS_RECV_RECORD)
            {
                for (unsigned j = 0; j < record.size; ++j)
                {
                    buffer[j] = record.data[j];
                }
            }
            transferred += record.size;
            hackos_tls_ack(tls, operation, record.size);
        }
        return true;
    }

    uint64_t Write(uint64_t socket, HackosTcpStream& record)
    {
        if (!tls)
        {
            return syscall_tcp_stream(HACKOS_TCP_WRITE, socket, &record);
        }
        if (error)
        {
            return HACKOS_ERROR;
        }
        size_t capacity;
        uint8_t* buffer = hackos_tls_buffer(tls, TLS_SEND_APP, &capacity);
        if (!buffer || !capacity)
        {
            return HACKOS_RETRY;
        }
        if (record.size > capacity)
        {
            record.size = capacity;
        }
        for (unsigned i = 0; i < record.size; ++i)
        {
            buffer[i] = record.data[i];
        }
        hackos_tls_ack(tls, TLS_SEND_APP, record.size);
        hackos_tls_flush(tls);
        return 0;
    }

    uint64_t Read(uint64_t socket, HackosTcpStream& record)
    {
        if (!tls)
        {
            return syscall_tcp_stream(HACKOS_TCP_READ, socket, &record);
        }
        if (error)
        {
            return HACKOS_ERROR;
        }
        size_t capacity;
        uint8_t* buffer = hackos_tls_buffer(tls, TLS_RECV_APP, &capacity);
        if (!buffer || !capacity)
        {
            if (!eof)
            {
                return HACKOS_RETRY;
            }
            record.size = 0;
            return 0;
        }
        if (record.size > capacity)
        {
            record.size = capacity;
        }
        for (unsigned i = 0; i < record.size; ++i)
        {
            record.data[i] = buffer[i];
        }
        hackos_tls_ack(tls, TLS_RECV_APP, record.size);
        return 0;
    }
};
}
