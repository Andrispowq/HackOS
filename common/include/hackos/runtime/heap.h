#pragma once
#include "hackos/runtime/memory.h"

// Single-threaded userspace allocator. The grow callback commits zeroed pages;
// metadata and payloads stay entirely inside the process's private heap.
class UserHeap
{
    struct alignas(16) Block
    {
        size_t size;
        Block* next;
        bool available;
    };

    unsigned char* base = nullptr;
    size_t committed = 0, limit = 0;
    Block* first = nullptr;
    bool (*grow)(size_t) = nullptr;

    void Merge()
    {
        for (auto block = first; block && block->next;)
        {
            if (block->available && block->next->available)
            {
                block->size += sizeof(Block) + block->next->size;
                block->next = block->next->next;
            }
            else
            {
                block = block->next;
            }
        }
    }

  public:
    void Initialise(void* address, size_t capacity, bool (*commit)(size_t))
    {
        base = (unsigned char*)address;
        limit = capacity;
        grow = commit;
    }

    void* Allocate(size_t size)
    {
        if (!size || size > limit || size > (size_t)-1 - 15)
        {
            return nullptr;
        }

        size = (size + 15) & ~(size_t)15;
        for (;;)
        {
            Block* last = nullptr;
            for (auto block = first; block; block = block->next)
            {
                last = block;
                if (!block->available || block->size < size)
                {
                    continue;
                }

                if (block->size >= size + sizeof(Block) + 16)
                {
                    auto remainder = (Block*)((unsigned char*)(block + 1) + size);
                    *remainder = {block->size - size - sizeof(Block), block->next, true};
                    block->next = remainder;
                    block->size = size;
                }

                block->available = false;
                return block + 1;
            }

            size_t needed = last && last->available ? size - last->size : size + sizeof(Block);
            size_t extra = (needed + 4095) & ~(size_t)4095;
            if (!base || extra > limit - committed || !grow(committed + extra))
            {
                return nullptr;
            }

            auto block = (Block*)(base + committed);
            *block = {extra - sizeof(Block), nullptr, true};
            if (last)
            {
                last->next = block;
            }
            else
            {
                first = block;
            }

            committed += extra;
            Merge();
        }
    }

    void Free(void* pointer)
    {
        for (auto block = first; block; block = block->next)
        {
            if (block + 1 == pointer)
            {
                block->available = true;
                Merge();
                return;
            }
        }
    }

    void* Reallocate(void* pointer, size_t size)
    {
        if (!pointer)
        {
            return Allocate(size);
        }

        if (!size)
        {
            Free(pointer);
            return nullptr;
        }

        for (auto block = first; block; block = block->next)
        {
            if (block + 1 == pointer && !block->available)
            {
                if (size <= block->size)
                {
                    return pointer;
                }

                void* replacement = Allocate(size);
                if (replacement)
                {
                    memcpy(replacement, pointer, block->size);
                    Free(pointer);
                }

                return replacement;
            }
        }

        return nullptr;
    }
};
