#include "streams.h"
#include "address_space.h"
#include "proc/tasking/process.h"
#include "proc/tasking/deadline.h"
#include "windows.h"
#include "arch/x86_64/interrupts/guard.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/screen/screen.h"
#include "fs/filesystem.h"
#include "lib/string.h"

struct Pipe
{
    unsigned readers = 0, writers = 0;
    unsigned head = 0, count = 0;
    uint8_t data[HACKOS_PIPE_CAPACITY] = {};
};

static Stream streams[128];
static Pipe pipes[16];
static Stream terminalRead = {0, StreamKind::TerminalRead};
static Stream terminalWrite = {0, StreamKind::TerminalWrite};
uint64_t user_streams_live, user_pipes_live;

Stream* AllocateStream()
{
    for (auto& stream : streams)
    {
        if (!stream.references)
        {
            stream = {};
            stream.references = 1;
            ++user_streams_live;
            return &stream;
        }
    }

    return nullptr;
}

int FreeDescriptor(Process* process)
{
    for (unsigned i = 0; i < HACKOS_FD_LIMIT; ++i)
    {
        if (!process->descriptors[i])
        {
            return i;
        }
    }

    return -1;
}

static Stream* Descriptor(uint64_t fd)
{
    return fd < HACKOS_FD_LIMIT ? GetRunningProcess()->descriptors[fd] : nullptr;
}

uint64_t UserTerminalInfo(uint64_t fd, HackosTerminalInfo* destination)
{
    auto stream = Descriptor(fd);
    if (!stream)
    {
        return HACKOS_ERROR;
    }

    HackosTerminalInfo info = {};
    if (stream->kind == StreamKind::TerminalRead || stream->kind == StreamKind::TerminalWrite)
    {
        const auto& console = Display::SharedDisplay()->console;
        info.columns = console.con_width;
        info.rows = console.con_height;
        info.flags = HACKOS_TERMINAL;
        if (CanReadTerminal(GetRunningProcess()->pid))
        {
            info.flags |= HACKOS_TERMINAL_FOREGROUND;
        }
    }

    return GetRunningProcess()->userSpace->CopyToUser((uint64_t)destination, &info, sizeof(info)) ? 0 : HACKOS_ERROR;
}

static uint64_t Release(Stream* stream)
{
    if (!stream || --stream->references)
    {
        return 0;
    }

    if (stream == &terminalRead || stream == &terminalWrite)
    {
        return 0;
    }

    uint64_t result = 0;
    if (stream->file)
    {
        if ((stream->file->accessMode & HACKOS_OPEN_WRITE) && filesystems[0]->Flush())
        {
            result = HACKOS_ERROR;
        }

        filesystems[0]->CloseFile(stream->file);
        --user_files_live;
    }

    if (stream->pipe)
    {
        auto pipe = stream->pipe;
        if (stream->kind == StreamKind::PipeRead)
        {
            --pipe->readers;
        }
        else
        {
            --pipe->writers;
        }

        if (!pipe->readers && !pipe->writers)
        {
            *pipe = {};
            --user_pipes_live;
        }

        WakeStreamWaiters();
    }

    *stream = {};
    --user_streams_live;
    return result;
}

void InheritStreams(Process* child, Process* parent)
{
    for (unsigned i = 0; i < HACKOS_FD_LIMIT; ++i)
    {
        auto stream = parent && parent->userSpace ? parent->descriptors[i]
                      : i == 0                    ? &terminalRead
                      : i < 3                     ? &terminalWrite
                                                  : nullptr;
        child->descriptors[i] = stream;
        if (stream)
        {
            ++stream->references;
        }
    }
}

void ReleaseStreams(Process* process, bool pipesOnly)
{
    for (auto& stream : process->descriptors)
    {
        if (stream && (!pipesOnly || stream->pipe))
        {
            Release(stream);
            stream = nullptr;
        }
    }
}

void SelectStreams(Process* child, Process* parent, const HackosSpawnIo& specification)
{
    // Retain before replacing: duplicate mappings and closed standard streams
    // must work without leaking any of the parent's extra pipe ends.
    Stream* selected[3] = {};
    for (unsigned i = 0; i < 3; ++i)
    {
        uint64_t fd = specification.streams[i];
        if (fd != HACKOS_ERROR)
        {
            selected[i] = parent->descriptors[fd];
            ++selected[i]->references;
        }
    }

    ReleaseStreams(child);
    for (unsigned i = 0; i < 3; ++i)
    {
        child->descriptors[i] = selected[i];
    }
}

bool StreamPathBusy(const char* path, bool writersOnly)
{
    for (auto& stream : streams)
    {
        if (stream.references && stream.file && !strcmp((char*)stream.file->GetPath(), (char*)path) &&
            (!writersOnly || (stream.file->accessMode & HACKOS_OPEN_WRITE)))
        {
            return true;
        }
    }

    return false;
}

uint64_t UserFdClose(uint64_t fd)
{
    auto stream = Descriptor(fd);
    if (!stream)
    {
        return HACKOS_ERROR;
    }

    GetRunningProcess()->descriptors[fd] = nullptr;
    return Release(stream);
}

uint64_t UserDup2(uint64_t from, uint64_t to)
{
    auto stream = Descriptor(from);
    if (!stream || to >= HACKOS_FD_LIMIT)
    {
        return HACKOS_ERROR;
    }

    if (from != to)
    {
        ++stream->references;
        Release(GetRunningProcess()->descriptors[to]);
        GetRunningProcess()->descriptors[to] = stream;
    }

    return to;
}

uint64_t UserPipe(uint64_t* destination)
{
    auto process = GetRunningProcess();
    if (!process->userSpace->WritableRange((uint64_t)destination, 2 * sizeof(uint64_t)))
    {
        return HACKOS_ERROR;
    }

    unsigned available = 0;
    for (auto stream : process->descriptors)
    {
        available += !stream;
    }

    if (available < 2)
    {
        return HACKOS_ERROR;
    }

    Pipe* pipe = nullptr;
    for (auto& candidate : pipes)
    {
        if (!candidate.readers && !candidate.writers)
        {
            pipe = &candidate;
            break;
        }
    }

    if (!pipe)
    {
        return HACKOS_ERROR;
    }

    auto reader = AllocateStream();
    auto writer = AllocateStream();
    if (!reader || !writer)
    {
        Release(reader);
        Release(writer);
        return HACKOS_ERROR;
    }

    *pipe = {};
    pipe->readers = pipe->writers = 1;
    ++user_pipes_live;
    reader->kind = StreamKind::PipeRead;
    writer->kind = StreamKind::PipeWrite;
    reader->pipe = writer->pipe = pipe;
    uint64_t result[2];
    result[0] = FreeDescriptor(process);
    process->descriptors[result[0]] = reader;
    result[1] = FreeDescriptor(process);
    process->descriptors[result[1]] = writer;
    process->userSpace->CopyToUser((uint64_t)destination, result, sizeof(result));
    return 0;
}

static uint64_t FileRead(Stream* stream, void* destination, uint64_t length)
{
    PreemptionGuard guard;
    auto file = stream->file;
    if (!(file->accessMode & HACKOS_OPEN_READ))
    {
        return HACKOS_DENIED;
    }

    uint64_t remaining = file->seek_position < file->GetSize() ? file->GetSize() - file->seek_position : 0;
    if (length > remaining)
    {
        length = remaining;
    }

    char buffer[HACKOS_IO_LIMIT];
    if (length && filesystems[0]->Read(file, buffer, length))
    {
        return HACKOS_ERROR;
    }

    GetRunningProcess()->userSpace->CopyToUser((uint64_t)destination, buffer, length);
    return length;
}

uint64_t UserFdRead(uint64_t fd, void* destination, uint64_t length, uint64_t timeout)
{
    auto process = GetRunningProcess();
    auto stream = Descriptor(fd);
    WaitDeadline deadline(timeout);
    if (!stream || !deadline.Valid() || length > HACKOS_IO_LIMIT ||
        !process->userSpace->WritableRange((uint64_t)destination, length))
    {
        return HACKOS_ERROR;
    }

    if (stream->kind == StreamKind::File)
    {
        return FileRead(stream, destination, length);
    }

    InterruptGuard guard;
    if (stream->kind != StreamKind::PipeRead && stream->kind != StreamKind::TerminalRead)
    {
        return HACKOS_DENIED;
    }

    if (!length)
    {
        return 0;
    }

    char data[HACKOS_IO_LIMIT];
    for (;;)
    {
        if (stream->kind == StreamKind::TerminalRead)
        {
            if (!CanReadTerminal(process->pid))
            {
                return HACKOS_DENIED;
            }

            char ch;
            auto result = ReadKeyboard(ch);
            if (result == KeyboardRead::Overflow)
            {
                return HACKOS_RETRY;
            }

            if (result == KeyboardRead::Character)
            {
                // Decoded byte input, without echo or line discipline. Ctrl-D
                // marks EOF for this read; terminal_read retains key semantics.
                if (ch == 4)
                {
                    return 0;
                }

                process->userSpace->CopyToUser((uint64_t)destination, &ch, 1);
                return 1;
            }
        }
        else
        {
            auto pipe = stream->pipe;
            if (pipe->count)
            {
                if (length > pipe->count)
                {
                    length = pipe->count;
                }

                for (unsigned i = 0; i < length; ++i)
                {
                    data[i] = pipe->data[pipe->head];
                    pipe->head = (pipe->head + 1) % HACKOS_PIPE_CAPACITY;
                }

                pipe->count -= length;
                process->userSpace->CopyToUser((uint64_t)destination, data, length);
                WakeStreamWaiters();
                return length;
            }

            if (!pipe->writers)
            {
                return 0;
            }
        }

        if (!timeout || deadline.Expired())
        {
            return timeout ? HACKOS_TIMED_OUT : HACKOS_WOULD_BLOCK;
        }

        deadline.Block(PROCESS_STATE_IO);
    }
}

uint64_t UserFdWrite(uint64_t fd, const void* source, uint64_t length, uint64_t timeout)
{
    auto process = GetRunningProcess();
    auto stream = Descriptor(fd);
    WaitDeadline deadline(timeout);
    char data[HACKOS_IO_LIMIT];
    if (!stream || !deadline.Valid() || length > sizeof(data) ||
        !process->userSpace->CopyFromUser(data, (uint64_t)source, length))
    {
        return HACKOS_ERROR;
    }

    if (stream->kind == StreamKind::File)
    {
        PreemptionGuard guard;
        auto file = stream->file;
        if (!(file->accessMode & HACKOS_OPEN_WRITE))
        {
            return HACKOS_DENIED;
        }

        if (!length)
        {
            return 0;
        }

        uint64_t offset = stream->append ? file->GetSize() : file->seek_position;
        if (offset > HACKOS_FILE_LIMIT || length > HACKOS_FILE_LIMIT - offset)
        {
            return HACKOS_ERROR;
        }

        file->seek_position = offset;
        return filesystems[0]->Write(file, data, length) ? HACKOS_ERROR : length;
    }

    if (stream->kind == StreamKind::TerminalWrite)
    {
        PreemptionGuard guard;
        for (uint64_t i = 0; i < length; ++i)
        {
            Display::SharedDisplay()->putc(data[i]);
        }

        return length;
    }

    InterruptGuard guard;
    if (stream->kind != StreamKind::PipeWrite)
    {
        return HACKOS_DENIED;
    }

    if (!length)
    {
        return 0;
    }

    auto pipe = stream->pipe;
    for (;;)
    {
        if (!pipe->readers)
        {
            return HACKOS_BROKEN_PIPE;
        }

        // Bounded writes are atomic, including writes from inherited writers.
        if (HACKOS_PIPE_CAPACITY - pipe->count >= length)
        {
            for (unsigned i = 0; i < length; ++i)
            {
                pipe->data[(pipe->head + pipe->count++) % HACKOS_PIPE_CAPACITY] = data[i];
            }

            WakeStreamWaiters();
            return length;
        }

        if (!timeout || deadline.Expired())
        {
            return timeout ? HACKOS_TIMED_OUT : HACKOS_WOULD_BLOCK;
        }

        deadline.Block(PROCESS_STATE_IO);
    }
}

static uint32_t Readiness(Stream* stream, uint32_t events)
{
    if (!stream)
    {
        return HACKOS_POLL_INVALID;
    }

    if (stream->kind == StreamKind::TerminalRead)
    {
        if (!CanReadTerminal(GetRunningProcess()->pid))
        {
            return HACKOS_POLL_ERROR;
        }

        return KeyboardReady() ? events & HACKOS_POLL_READ : 0;
    }

    if (stream->kind == StreamKind::TerminalWrite)
    {
        return events & HACKOS_POLL_WRITE;
    }

    if (stream->kind == StreamKind::File)
    {
        return events & stream->file->accessMode & (HACKOS_POLL_READ | HACKOS_POLL_WRITE);
    }

    auto pipe = stream->pipe;
    if (stream->kind == StreamKind::PipeRead)
    {
        return (pipe->count ? events & HACKOS_POLL_READ : 0) | (!pipe->writers ? HACKOS_POLL_HANGUP : 0);
    }

    return !pipe->readers ? HACKOS_POLL_ERROR : pipe->count < HACKOS_PIPE_CAPACITY ? events & HACKOS_POLL_WRITE : 0;
}

uint64_t UserPoll(HackosPollFd* destination, uint64_t count, uint64_t timeout)
{
    InterruptGuard guard;
    auto space = GetRunningProcess()->userSpace;
    WaitDeadline deadline(timeout);
    HackosPollFd items[HACKOS_FD_LIMIT];
    if (!deadline.Valid() || count > HACKOS_FD_LIMIT ||
        !space->WritableRange((uint64_t)destination, count * sizeof(*items)) ||
        !space->CopyFromUser(items, (uint64_t)destination, count * sizeof(*items)))
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < count; ++i)
    {
        if (items[i].events & ~(HACKOS_POLL_READ | HACKOS_POLL_WRITE | HACKOS_POLL_WINDOW) ||
            ((items[i].events & HACKOS_POLL_WINDOW) && items[i].events != (HACKOS_POLL_WINDOW | HACKOS_POLL_READ)))
        {
            return HACKOS_ERROR;
        }
    }

    for (;;)
    {
        unsigned ready = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            items[i].revents = (items[i].events & HACKOS_POLL_WINDOW)
                                   ? UserWindowReadiness(items[i].fd)
                                   : Readiness(Descriptor(items[i].fd), items[i].events);
            ready += items[i].revents != 0;
        }

        if (ready || deadline.Expired())
        {
            space->CopyToUser((uint64_t)destination, items, count * sizeof(*items));
            return ready;
        }

        deadline.Block(PROCESS_STATE_IO);
    }
}

uint64_t UserFdSeek(uint64_t fd, uint64_t offset)
{
    auto stream = Descriptor(fd);
    if (!stream || !stream->file || offset > HACKOS_FILE_LIMIT)
    {
        return HACKOS_ERROR;
    }

    stream->file->seek_position = offset;
    return offset;
}

uint64_t UserFdFlush(uint64_t fd)
{
    auto stream = Descriptor(fd);
    return stream && stream->file && !filesystems[0]->Flush() ? 0 : HACKOS_ERROR;
}
