#include "memory.h"
#include "hackos/runtime/heap.h"
#include "hackos/syscall.h"

static UserHeap heap;
static bool initialised;

static bool Grow(size_t bytes)
{
    return syscall_heap_resize(bytes) == HACKOS_HEAP_BASE;
}

extern "C" void* malloc(size_t size)
{
    if (!initialised)
    {
        heap.Initialise((void*)HACKOS_HEAP_BASE, HACKOS_HEAP_LIMIT, Grow);
        initialised = true;
    }

    return heap.Allocate(size);
}

extern "C" void free(void* pointer)
{
    heap.Free(pointer);
}

extern "C" void* calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size)
    {
        return nullptr;
    }

    void* pointer = malloc(count * size);
    if (pointer)
    {
        memset(pointer, 0, count * size);
    }

    return pointer;
}

extern "C" void* realloc(void* pointer, size_t size)
{
    return pointer ? heap.Reallocate(pointer, size) : malloc(size);
}

void* operator new(size_t size)
{
    void* pointer = malloc(size ? size : 1);
    if (!pointer)
    {
        syscall_exit(127);
        for (;;)
        {
            syscall_yield();
        }
    }

    return pointer;
}

void* operator new[](size_t size)
{
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept
{
    free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    free(pointer);
}

void operator delete(void* pointer, size_t) noexcept
{
    free(pointer);
}

void operator delete[](void* pointer, size_t) noexcept
{
    free(pointer);
}
