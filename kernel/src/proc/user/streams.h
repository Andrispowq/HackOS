#pragma once
#include "hackos/abi.h"

class Process;
class ActiveFile;
struct Pipe;

enum class StreamKind
{
    TerminalRead,
    TerminalWrite,
    File,
    PipeRead,
    PipeWrite
};

struct Stream
{
    unsigned references = 0;
    StreamKind kind = StreamKind::File;
    ActiveFile* file = nullptr;
    Pipe* pipe = nullptr;
    bool append = false;
};

extern uint64_t user_streams_live, user_pipes_live;
void InheritStreams(Process* child, Process* parent);
void ReleaseStreams(Process* process, bool pipesOnly = false);
bool StreamPathBusy(const char* path, bool writersOnly = false);
Stream* AllocateStream();
int FreeDescriptor(Process* process);
uint64_t UserFdRead(uint64_t fd, void* destination, uint64_t length, uint64_t timeout);
uint64_t UserFdWrite(uint64_t fd, const void* source, uint64_t length, uint64_t timeout);
uint64_t UserFdClose(uint64_t fd);
uint64_t UserDup2(uint64_t from, uint64_t to);
uint64_t UserPipe(uint64_t* destination);
uint64_t UserPoll(HackosPollFd* destination, uint64_t count, uint64_t timeout);
uint64_t UserFdSeek(uint64_t fd, uint64_t offset);
uint64_t UserFdFlush(uint64_t fd);
void SelectStreams(Process* child, Process* parent, const HackosSpawnIo& specification);
