#pragma once
#include "hackos/user.h"
#include "hackos/stdio.h"
#include "hackos/runtime/http.h"

// Incremental HTTP transport. Step never waits; the GUI owns the timed poll.
class Fetch
{
    Hackos::Http::Url url;
    HackosResolve dns = {};
    Hackos::Http::Decoder decoder;
    char request[1536];
    unsigned requestSize = 0, sent = 0;
    uint64_t started = 0, progress = 0;
    enum { Idle, Resolving, Sending, Reading } phase = Idle;

    static bool Headers(void* context, unsigned code, const char*)
    {
        auto& self = *(Fetch*)context;
        self.status = code;
        return code >= 200 && code < 300;
    }

    static bool Body(void* context, const uint8_t* bytes, unsigned size)
    {
        auto& self = *(Fetch*)context;
        if (size > sizeof(self.body) - self.size)
        {
            return false;
        }
        for (unsigned i = 0; i < size; ++i)
        {
            self.body[self.size++] = bytes[i];
        }
        return true;
    }

    void Connect(const uint8_t* address)
    {
        HackosTcpStream record = {};
        for (unsigned i = 0; i < 4; ++i)
        {
            record.address[i] = address[i];
        }
        record.peerPort = url.port;
        socket = syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, &record);
        if (!socket || socket >> 63)
        {
            socket = 0;
            Fail("Cannot connect: network unavailable or sockets busy.");
            return;
        }
        phase = Sending;
    }

  public:
    uint64_t socket = 0;
    char body[Hackos::Http::BodyLimit];
    unsigned size = 0, status = 0;
    bool done = false, good = false;
    const char* error = nullptr;

    Fetch() : decoder(Body, Headers, this)
    {
    }
    ~Fetch()
    {
        Cancel();
    }

    bool Busy() const
    {
        return phase != Idle;
    }
    unsigned Events() const
    {
        return phase == Sending ? HACKOS_POLL_WRITE : HACKOS_POLL_READ;
    }

    void Cancel()
    {
        if (phase == Resolving)
        {
            syscall_resolve(HACKOS_RESOLVE_CANCEL, nullptr);
        }
        if (socket)
        {
            syscall_tcp_stream(HACKOS_TCP_CLOSE, socket, nullptr);
        }
        socket = 0;
        phase = Idle;
    }

    void Fail(const char* message)
    {
        Cancel();
        error = message;
        done = true;
        good = false;
    }

    void Start(const char* address, uint64_t now)
    {
        Cancel();
        done = good = false;
        error = nullptr;
        size = status = sent = 0;
        started = progress = now;
        decoder = Hackos::Http::Decoder(Body, Headers, this);
        if (!Hackos::Http::ParseUrl(address, url))
        {
            Fail("Use an http:// URL. HTTPS is not supported yet.");
            return;
        }
        char authority[272];
        if (url.port == 80)
        {
            snprintf(authority, sizeof(authority), "%s", url.host);
        }
        else
        {
            snprintf(authority, sizeof(authority), "%s:%u", url.host, url.port);
        }
        int n = snprintf(request, sizeof(request),
                         "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: HackOS-Browser/0.1\r\nAccept: text/html,text/plain\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                         url.target, authority);
        if (n <= 0 || n >= (int)sizeof(request))
        {
            Fail("Request too large.");
            return;
        }
        requestSize = n;
        uint8_t ip[4];
        if (Hackos::ParseIPv4(url.host, ip))
        {
            Connect(ip);
        }
        else
        {
            dns = {};
            User::Copy(dns.name, url.host);
            if (syscall_resolve(HACKOS_RESOLVE_START, &dns))
            {
                Fail("Cannot start DNS lookup.");
            }
            else
            {
                phase = Resolving;
            }
        }
    }

    void Step(uint64_t now)
    {
        if (!Busy())
        {
            return;
        }
        if (now - started >= 30000 || now - progress >= 10000)
        {
            Fail("Page load timed out. Check the address or try Reload.");
            return;
        }
        if (phase == Resolving)
        {
            auto result = syscall_resolve(HACKOS_RESOLVE_QUERY, &dns);
            if (!result && dns.state == HACKOS_RESOLVE_PENDING)
            {
                return;
            }
            syscall_resolve(HACKOS_RESOLVE_CANCEL, nullptr);
            phase = Idle;
            if (result || dns.state != HACKOS_RESOLVE_REPLY)
            {
                Fail("DNS lookup failed.");
            }
            else
            {
                progress = now;
                Connect(dns.address);
            }
            return;
        }
        if (phase == Sending)
        {
            HackosTcpStream record = {};
            if (syscall_tcp_stream(HACKOS_TCP_STATUS, socket, &record) || record.state != HACKOS_TCP_PENDING)
            {
                Fail("Connection failed.");
                return;
            }
            if (!(record.flags & HACKOS_TCP_WRITABLE))
            {
                return;
            }
            record.size = requestSize - sent;
            if (record.size > sizeof(record.data))
            {
                record.size = sizeof(record.data);
            }
            for (unsigned i = 0; i < record.size; ++i)
            {
                record.data[i] = request[sent + i];
            }
            if (syscall_tcp_stream(HACKOS_TCP_WRITE, socket, &record))
            {
                Fail("Request write failed.");
                return;
            }
            sent += record.size;
            progress = now;
            if (sent == requestSize)
            {
                phase = Reading;
            }
        }
        if (phase == Reading)
        {
            // Bound work per GUI iteration even when the peer sends continuously.
            for (unsigned i = 0; i < 8; ++i)
            {
                HackosTcpStream record = {};
                record.size = sizeof(record.data);
                auto result = syscall_tcp_stream(HACKOS_TCP_READ, socket, &record);
                if (result == HACKOS_RETRY)
                {
                    return;
                }
                if (result)
                {
                    Fail("Connection lost before the page completed.");
                    return;
                }
                progress = now;
                if (!(record.size ? decoder.Feed(record.data, record.size) : decoder.End()))
                {
                    Fail(status && (status < 200 || status >= 300) ? "HTTP error or redirect (redirects are not followed)." : decoder.Error());
                    return;
                }
                if (decoder.Done())
                {
                    Cancel();
                    done = good = true;
                    return;
                }
            }
        }
    }
};
