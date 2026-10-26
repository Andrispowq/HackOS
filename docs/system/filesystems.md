# Filesystems

[System index](index.md) · [Storage devices](devices.md)

File access is layered through a small VFS, a filesystem interface, and a
FAT32 driver. All persistent storage currently reaches a `Device` that performs
sector I/O.

```mermaid
flowchart LR
    Caller[Kernel or syscall caller] --> VFS[VFS functions]
    VFS --> FS[Filesystem and ActiveFile]
    FS --> FAT[FAT32 adapter]
    FAT --> Driver[FAT32Driver]
    Driver --> Device[Device: ATA or AHCI]
```

## Mounting and partition assumptions

[LocateFilesystemsFAT32](../../kernel/src/fs/fat32/fat32.cpp) scans the relocated
BIOS MBR for type `0x0B` partitions. UEFI assumes an unpartitioned FAT32 volume
at LBA 0. These assumptions match the project's [image layouts](boot.md).

There is an additional coupling in the
[FAT32Driver constructor](../../kernel/src/fs/fat32/fat32driver.cpp): it replaces
the supplied partition offset with the second BIOS partition's offset, or zero
for UEFI. General partition discovery therefore needs both the discovery code
and the driver constructor to change.

## Interfaces and paths

[filesystem.h](../../kernel/src/fs/filesystem.h) defines:

- `Filesystem`: open/close, create/resize/delete, enumeration, and byte reads/writes.
- `ActiveFile`: file metadata accessors, a seek position, and an owning
  filesystem index.
- A registry of filesystem instances, sized from the device registry.

[vfs.cpp](../../kernel/src/fs/vfs.cpp) exposes `fopen`, `fread`, `fwrite`, and
`fclose`. A VFS path such as `fs0:/USR/HELLO_2.TXT` selects filesystem 0 and is
translated to `~/USR/HELLO_2.TXT` for the filesystem implementation. The shell
and ELF loader also use `~/...` directly against `filesystems[0]`.

These interfaces resemble stdio but have different contracts. `fread` and
`fwrite` multiply `size * n` and return the filesystem's status; the FAT32
adapter currently returns zero, rather than a count of transferred elements.
`O_CREAT` triggers creation on a missing file, while access enforcement for
`O_READ` and `O_WRITE` is not implemented in the VFS dispatch.

The VFS needs bounds and failure handling before it can be a userspace boundary:
filesystem indices are parsed into a small buffer, missing-file results can
be dereferenced, and read/write/close assume a usable handle. The returned
`void*` is currently a kernel object pointer, not an isolated file descriptor.

## FAT32 implementation

[fat32.cpp](../../kernel/src/fs/fat32/fat32.cpp) adapts the common interfaces to
`FAT32_ActiveFile` and `FAT32Driver`. The lower-level driver handles:

- Reading the BPB and calculating FAT/data-region offsets and cluster sizes.
- Caching FAT entries and walking, allocating, resizing, and freeing cluster chains.
- Directory enumeration and modification, including short/long filename records.
- Path traversal and file creation, reading, writing, resizing, and deletion.

Reads/writes advance the handle's seek position. The adapter currently advances
by the requested size and discards the lower-level result, so EOF, partial I/O,
and errors need a deliberate contract rather than assuming stdio semantics.

## Persistence and ownership

Cluster writes go through the storage driver. FAT metadata is cached, and the
driver destructor writes the boot sector, backup boot sector, and FAT copies.
Closing an `ActiveFile` does not destroy the filesystem driver. A defined
flush/unmount operation is needed to make persistence reliable across shutdown,
restart, or emulator termination.

Use disposable disk images for write experiments. A meaningful filesystem test
must reopen the image after the intended flush boundary, not only read data
back from the same in-memory cache. The existing `Device` boundary is a good
place for an in-memory disk adapter; see the [testing plan](../development/testing.md).

Priority fixes and malformed-image cases are listed in [TODO](../TODO.md).
