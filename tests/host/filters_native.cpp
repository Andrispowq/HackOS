#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

extern int FilterMain(int, char**);

static unsigned long Result(long result)
{
    return result >= 0 ? (unsigned long)result : errno == EPIPE ? ~9UL : ~0UL;
}

static bool Fault(const char* name, unsigned long fd)
{
    const char* setting = std::getenv(name);
    return setting && std::strtoul(setting, nullptr, 10) == fd;
}

static unsigned long Chunk(const char* name, unsigned long length)
{
    const char* setting = std::getenv(name);
    unsigned long limit = setting ? std::strtoul(setting, nullptr, 10) : 0;
    return limit && limit < length ? limit : length;
}

extern "C" unsigned long host_open(const char* path, unsigned long mode)
{
    int flags = mode & 2 ? O_WRONLY : O_RDONLY;
    flags |= mode & 4 ? O_CREAT : 0;
    flags |= mode & 8 ? O_TRUNC : 0;
    flags |= mode & 16 ? O_APPEND : 0;
    int fd = open(path, flags, 0644);
    if (fd >= 16)
    {
        close(fd);
        return ~0UL;
    }
    return Result(fd);
}

extern "C" unsigned long host_read(unsigned long fd, void* data, unsigned long length)
{
    if (Fault("FILTER_READ_FAIL", fd))
    {
        return ~0UL;
    }
    return Result(read(fd, data, Chunk("FILTER_READ_CHUNK", length)));
}

extern "C" unsigned long host_write(unsigned long fd, const void* data, unsigned long length)
{
    if (Fault("FILTER_WRITE_FAIL", fd))
    {
        return ~0UL;
    }
    return Result(write(fd, data, Chunk("FILTER_WRITE_CHUNK", length)));
}

extern "C" unsigned long host_close(unsigned long fd)
{
    auto result = Result(close(fd));
    return Fault("FILTER_CLOSE_FAIL", fd) ? ~0UL : result;
}

extern "C" unsigned long host_terminal_info(unsigned long fd, void* output)
{
    if (fcntl(fd, F_GETFD) < 0)
    {
        return ~0UL;
    }
    auto info = (unsigned int*)output;
    for (unsigned i = 0; i < 4; ++i)
    {
        info[i] = 0;
    }
    const char* setting = std::getenv("FILTER_TERMINAL");
    if (fd == 1 && setting)
    {
        info[0] = 16;
        info[1] = 4;
        info[2] = std::strcmp(setting, "background") ? 3 : 1;
    }
    return 0;
}

extern "C" unsigned long host_terminal_read()
{
    static unsigned position;
    const char* keys = std::getenv("FILTER_KEYS");
    return keys && position < std::strlen(keys) ? (unsigned char)keys[position++] : 'q';
}

extern "C" unsigned long hackos_user___strlen(char* text)
{
    return std::strlen(text);
}

extern "C" char* hackos_user___strcpy(char* to, const char* from)
{
    return std::strcpy(to, from);
}

int main(int argc, char** argv)
{
    std::signal(SIGPIPE, SIG_IGN);
    return FilterMain(argc, argv);
}
