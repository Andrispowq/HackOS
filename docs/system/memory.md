# Memory management

[System index](index.md) · [Boot information](boot.md)

Memory management has three layers: a firmware memory map describes physical
regions, a bitmap allocator hands out 4 KiB frames, and page tables map those
frames into virtual addresses. The heap uses those mappings to provide smaller
allocations to the rest of the kernel.

## Firmware maps and physical frames

[MemoryMap](../../kernel/src/memory/memory_map.cpp) wraps the entries received
from the loader. It selects the usable-memory type according to `fromUEFI`;
BIOS E820 and EFI type values are not interchangeable.

[PageFrameAllocator](../../kernel/src/arch/x86_64/paging/page_frame_allocator.cpp)
places its bitmap in a usable region, initially reserves memory, then releases
usable regions into the free pool. It protects the first MiB, the kernel image,
and its own bitmap. `RequestPage` marks a free frame used and returns its
physical address. Exhaustion currently prints an error and halts.

`ReservePages`/`UnreservePages` describe memory unavailable to ordinary
allocation; `LockPages`/`FreePages` account for allocated memory. Callers must
balance these operations. The counters are adjusted on every call, so repeated
free/reserve operations are not automatically harmless.

The allocator currently sizes its bitmap from the sum of region lengths.
Physical maps can contain gaps: the sum of lengths is not necessarily the
highest physical address. Bitmap sizing, bounds, and initial mappings need to
be checked against sparse maps before supporting larger memory configurations.

## Page tables

[PageTableManager](../../kernel/src/arch/x86_64/paging/page_table_manager.cpp)
walks four levels of x86-64 page tables and allocates intermediate tables as
needed. Its main operations are:

| Operation | Current behavior |
| --- | --- |
| `MapMemory(virtual, physical, flags)` | Installs a 4 KiB mapping; default flags are present and writable |
| `PhysicalAddress(virtual)` | Returns the mapped frame base, without the byte offset within that page |
| `SetAsCurrent()` | Loads CR3 and updates `CurrentDirectory` |
| `Clone()` | Shares entries equal to the kernel mappings and copies other mappings through the clone helpers |

`KernelDirectory` is the global kernel address space. [InitPaging](../../kernel/src/arch/x86_64/paging/paging.cpp)
builds identity mappings up to `GetTotalSystemMemory()` and activates the new
directory. Device discovery adds mappings for hardware registers later.

The flag definitions include user access, cache controls, and execute-disable.
Their presence does not establish complete permission enforcement or process
isolation: intermediate-table flags, existing mappings, CR3 transitions, and
CPU feature setup all matter. Address translation also needs a defined failure
result for unmapped addresses instead of blindly walking absent entries.

## Early allocation and the heap

[kmalloc_int](../../kernel/src/lib/memory.cpp) uses a bump pointer after the
kernel image until the heap is initialized. Its early aligned variant rounds
to a page boundary. After initialization, allocation goes through
[heap.cpp](../../kernel/src/memory/heap.cpp).

The heap begins at virtual address `0x0000100000000000` with 16 mapped pages.
It uses linked `HeapSegmentHeader` records, rounds requests to 16-byte sizes,
splits sufficiently large free segments, and coalesces neighbors on free.
`ExpandHeap` maps more physical frames when no segment can satisfy a request.
Global C++ `new` and `delete` route through `kmalloc` and `kfree`.

Page alignment is not currently preserved by the post-initialization
`kmalloc_int` path: it calls `alloc(size)` without the requested alignment, and
the heap's alignment argument is unused. DMA and page-table code must not assume
that ordinary heap memory is page-aligned or physically contiguous.

## Working with memory code

Boot structures, firmware tables, framebuffer memory, and device buffers need
explicit lifetime and mapping rules. Allocation must not reclaim data still
referenced after a page-table switch. Kernel and user address-space teardown
also need a clear ownership model before task isolation can be enabled.

The support routines in [lib/memory.cpp](../../kernel/src/lib/memory.cpp) are
not drop-in standard libc functions. For example, `memset` writes word-sized
values on aligned portions and is used to fill pixels; `memcmp` returns an
equality-style result rather than a conventional ordering result. Changing these
semantics requires updating callers and the copies in other components.

See the [testing plan](../development/testing.md) for allocator, map, and
support-library cases, and [TODO](../TODO.md) for the immediate repairs.
