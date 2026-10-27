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
physical address. Exhaustion prints an error and halts; `TryRequestPage`
returns null instead and is used by the user-space loader.

`ReservePages`/`UnreservePages` describe memory unavailable to ordinary
allocation; `LockPages`/`FreePages` account for allocated memory. Callers must
balance these operations. Counters change only when the bitmap changes, but the
single bitmap does not distinguish every ownership category; callers must not
free a reserved frame as though they owned it.

Bitmap capacity follows the highest usable physical page, including gaps. It
starts fully reserved and releases only whole usable pages. Kernel bump
allocations and the page-aligned bitmap are protected. Large reserved PCI
windows do not inflate the allocator's RAM limit. Highly sparse maps still need
a more space-efficient representation and broader guest coverage.

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
identity-maps reported RAM/firmware regions and the low boot handoff area,
rather than treating the sum of region lengths as an address. High reserved
PCI windows are left to device discovery; EFI MMIO regions are mapped by their
consumers. Device discovery adds mappings for hardware registers later.

The flag definitions include user access, cache controls, and execute-disable.
The user loader sets permissions at every level, shares kernel mappings as
supervisor-only, and enables NX and supervisor write protection. `MapMemory`
invalidates changed active/shared mappings, including the reused AHCI buffer
window. Address translation also needs a defined failure
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
Global C++ `new` and `delete` route through `kmalloc` and `kfree`. Heap mutation
is protected from task preemption by nested guards; hardware IRQs stay enabled.
These guards assume a single scheduling CPU. Kernel threads share page tables
and idle frees an exited thread's stack only after switching away from it.

Page alignment is not currently preserved by the post-initialization
`kmalloc_int` path: it calls `alloc(size)` without the requested alignment, and
the heap's alignment argument is unused. DMA and page-table code must not assume
that ordinary heap memory is page-aligned or physically contiguous.

## Working with memory code

Boot structures, firmware tables, framebuffer memory, and device buffers need
explicit lifetime and mapping rules. Allocation must not reclaim data still
referenced after a page-table switch. User address spaces own a ledger of private frames/tables and release them
only after switching away; shared kernel mappings are never released with a user
task. The legacy clone helpers still need a broader ownership audit.

The support routines in [lib/memory.cpp](../../kernel/src/lib/memory.cpp) use
standard byte-memory semantics: `memset` repeats an unsigned byte, `memcmp`
compares unsigned bytes, and copies return the destination. Kernel/userspace
memory primitives use NASM helpers; BIOS stage 2 uses C. Framebuffer colour fills
use explicit pixel operations. String comparisons stop at NUL and `strcpy`
copies the terminator and returns the original destination.

See the [testing plan](../development/testing.md) for allocator, map, and
support-library cases, and [TODO](../TODO.md) for the immediate repairs.

Step 17 reserves a separate 512 KiB private user heap starting at
`0x0000400000100000`; ELF load segments remain below it. `heap_resize` grows a
page-rounded prefix transactionally, zeroes new frames, marks them writable/NX,
and rolls back allocation failure. Shrink releases whole pages and reloads CR3
before returning to user code. A separate 128-entry frame ledger keeps the heap
quota independent of the 64 image/stack-page quota. Exit reclamation releases both
ledgers after switching away. See [userspace](userspace.md) for allocator semantics.
