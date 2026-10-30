#include "resolve.h"
#include "hackos/runtime/http.h"

namespace
{
struct Socket
{
    uint64_t handle = 0;
    ~Socket()
    {
        if (handle) syscall_tcp_stream(HACKOS_TCP_CLOSE, handle, nullptr);
    }
};

bool Wait(uint64_t handle, uint32_t events)
{
    HackosPollFd item = {handle, HACKOS_POLL_TCP | events, 0};
    return syscall_poll(&item, 1, 10000) == 1 && (item.revents & events) &&
           !(item.revents & (HACKOS_POLL_ERROR | HACKOS_POLL_INVALID));
}

struct Output
{
    const char* path = nullptr;
    const char* error = nullptr;
    uint64_t fd = 1;
    bool terminal = false, opened = false, accepted = false;
    unsigned status = 0;

    ~Output()
    {
        if (opened) syscall_fd_close(fd);
    }

    static bool Headers(void* context, unsigned code, const char* location)
    {
        auto& out = *(Output*)context;
        out.status = code;
        char text[64];
        snprintf(text, sizeof(text), "HTTP status %u\n", code);
        Error(text);
        if (code < 200 || code >= 300)
        {
            Error(code >= 300 && code < 400 ? "http: redirect not followed.\n" : "http: unsuccessful HTTP status.\n");
            if (*location)
            {
                Error("Location: ");
                Error(location);
                Error("\n");
            }
            return true;
        }
        out.accepted = true;
        if (out.path)
        {
            out.fd = syscall_fd_open(out.path, HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_TRUNCATE);
            if (out.fd >= HACKOS_FD_LIMIT)
            {
                out.error = "cannot open output file";
                return false;
            }
            out.opened = true;
        }
        HackosTerminalInfo info = {};
        if (syscall_terminal_info(out.fd, &info))
        {
            out.error = "cannot inspect output";
            return false;
        }
        out.terminal = (info.flags & HACKOS_TERMINAL) != 0;
        return true;
    }

    static bool Body(void* context, const uint8_t* bytes, unsigned size)
    {
        auto& out = *(Output*)context;
        if (!out.accepted) return true;
        if (!out.terminal)
        {
            if (Write(out.fd, bytes, size)) return true;
            out.error = "output write failed (output may be partial)";
            return false;
        }
        // Preserve text layout, but never execute peer-controlled terminal escapes.
        char buffer[1024];
        unsigned used = 0;
        const char* hex = "0123456789abcdef";
        for (unsigned i = 0; i < size; ++i)
        {
            uint8_t c = bytes[i];
            if ((c >= 32 && c < 127) || c == '\n' || c == '\t')
            {
                buffer[used++] = c;
            }
            else
            {
                buffer[used++] = '\\';
                buffer[used++] = 'x';
                buffer[used++] = hex[c >> 4];
                buffer[used++] = hex[c & 15];
            }
            if (used > sizeof(buffer) - 4)
            {
                if (!Write(out.fd, buffer, used)) return false;
                used = 0;
            }
        }
        return !used || Write(out.fd, buffer, used);
    }
};

void CloseGracefully(uint64_t handle)
{
    syscall_tcp_stream(HACKOS_TCP_SHUTDOWN_WRITE, handle, nullptr);
    HackosTcpStream status = {};
    for (unsigned i = 0; i < 25; ++i)
    {
        if (syscall_tcp_stream(HACKOS_TCP_STATUS, handle, &status) || status.state != HACKOS_TCP_PENDING || status.queued) break;
        syscall_sleep(20);
    }
}
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: http [-o FILE] http://host[:port]/path\n"
                        "GET plain HTTP, up to 32 KiB. Status goes to stderr; body goes to stdout or FILE.\n"
                        "Terminal control bytes are escaped; files and pipes receive exact bytes.\n"
                        "HTTPS, compression and automatic redirects are not supported yet.\n"
                        "Demo: http http://10.0.2.2:8080/\n";
    if (argc == 2 && (Equal(argv[1], "--help") || Equal(argv[1], "-h")))
    {
        Print(usage);
        return 0;
    }
    Output output;
    const char* input = nullptr;
    if (argc == 2)
    {
        input = argv[1];
    }
    else if (argc == 4 && Equal(argv[1], "-o"))
    {
        output.path = argv[2];
        input = argv[3];
    }
    else
    {
        Error(usage);
        return 2;
    }
    Hackos::Http::Url url;
    if (!Hackos::Http::ParseUrl(input, url))
    {
        Error("http: invalid URL; use http:// with an IPv4 address or ASCII hostname. HTTPS requires TLS.\n");
        return 2;
    }
    HackosTcpStream record = {};
    if (!Hackos::ParseIPv4(url.host, record.address))
    {
        HackosResolve resolved = {};
        if (!ResolveHost(url.host, resolved)) return 1;
        for (unsigned i = 0; i < 4; ++i)
        {
            record.address[i] = resolved.address[i];
        }
    }
    record.peerPort = url.port;
    Socket socket;
    uint64_t handle = syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, &record);
    if (!handle || (handle >> 63))
    {
        Error("http: cannot open connection (unsupported destination or sockets busy).\n");
        return 1;
    }
    socket.handle = handle;
    char authority[272], request[1536];
    int hostLength = url.port == 80 ? snprintf(authority, sizeof(authority), "%s", url.host)
                                  : snprintf(authority, sizeof(authority), "%s:%u", url.host, url.port);
    int length = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: HackOS/0.1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                          url.target, authority);
    if (hostLength < 0 || hostLength >= (int)sizeof(authority) || length < 0 || length >= (int)sizeof(request))
    {
        Error("http: request too large.\n");
        return 1;
    }
    for (unsigned offset = 0; offset < (unsigned)length;)
    {
        if (!Wait(handle, HACKOS_POLL_WRITE))
        {
            Error("http: connection/write failed or timed out.\n");
            return 1;
        }
        record.size = length - offset;
        if (record.size > sizeof(record.data)) record.size = sizeof(record.data);
        for (unsigned i = 0; i < record.size; ++i)
        {
            record.data[i] = request[offset + i];
        }
        if (syscall_tcp_stream(HACKOS_TCP_WRITE, handle, &record))
        {
            Error("http: request write failed.\n");
            return 1;
        }
        offset += record.size;
    }
    Hackos::Http::Decoder decoder(Output::Body, Output::Headers, &output);
    while (!decoder.Done())
    {
        record.size = sizeof(record.data);
        uint64_t result = syscall_tcp_stream(HACKOS_TCP_READ, handle, &record);
        if (result == HACKOS_RETRY)
        {
            if (Wait(handle, HACKOS_POLL_READ)) continue;
            Error("http: response failed or timed out; output may be partial.\n");
            return 1;
        }
        if (result)
        {
            Error("http: connection lost; output may be partial.\n");
            return 1;
        }
        bool ok = record.size ? decoder.Feed(record.data, record.size) : decoder.End();
        if (!ok)
        {
            Error("http: ");
            Error(output.error ? output.error : decoder.Error());
            Error("; output may be partial.\n");
            return 1;
        }
        if (output.status && !output.accepted) return 1;
    }
    CloseGracefully(handle);
    if (output.opened && syscall_fd_flush(output.fd))
    {
        Error("http: output flush failed; file may be partial.\n");
        return 1;
    }
    char message[80];
    snprintf(message, sizeof(message), output.opened ? "Saved %u bytes.\n" : "Received %u bytes.\n", decoder.Received());
    Error(message);
    return 0;
}
