# Filesystems

[System index](index.md) · [Storage devices](devices.md)

File access passes through the VFS, the `Filesystem`/`ActiveFile` interfaces,
the FAT32 adapter, and finally a block `Device`.

```mermaid
flowchart LR
    Caller[Kernel or syscall caller] --> VFS[VFS functions]
    VFS --> FS[Filesystem and ActiveFile]
    FS --> FAT[FAT32 adapter]
    FAT --> Driver[FAT32Driver]
    Driver --> Device[Device: ATA or AHCI]
```

## Mounting and paths

[LocateFilesystemsFAT32](../../kernel/src/fs/fat32/fat32.cpp) scans the relocated
BIOS MBR for type `0x0B` partitions. UEFI assumes a FAT32 volume at LBA 0, matching
the generated images. The driver honors the partition offset supplied by this
discovery layer. Selecting a device/partition beyond these boot assumptions still
needs work.

The VFS translates `fs12:/USR/FILE` to `~/USR/FILE` on filesystem 12. It checks
the decimal volume index, path prefix, failed opens, null handles and registry
bounds before dispatch. The shell and ELF loader also use `~/...` directly
against `filesystems[0]`.

The returned `void*` is an `ActiveFile` object pointer. Null/index checks do not
make arbitrary pointers safe; these handles are not a protected userspace API.
Access-mode enforcement for `O_READ` and `O_WRITE` is also unfinished.

## File I/O contract

`fread`/`fwrite` return a status code, not a stdio element count: zero means
success and a negative result indicates failure. FAT32 reads clamp to EOF and
advance the seek position only by the bytes actually read. A read starting past
EOF succeeds without changing the buffer or position. Writes advance on success,
grow the file when needed, and preserve bytes outside the written range.

New regular files start empty. Resizing to zero releases the entire chain;
growth zeroes newly exposed bytes, including gaps before a seeked write.
Read-only FAT entries reject modification. File sizes remain limited to FAT32's
32-bit size field. The common resize/delete API still returns `void`, so callers
cannot observe every failed metadata operation through that interface.

## FAT32 structures

[FAT32Driver](../../kernel/src/fs/fat32/fat32driver.cpp) validates the BPB before
allocating buffers: signatures, 512-byte sector size, power-of-two cluster size,
FAT count/capacity, data extent, root cluster, and active FAT selection. The
block-device API currently has no capacity query, so it cannot yet validate the
claimed volume extent against the actual device size.

Cluster chains are range-checked and cyclic chains are rejected. Allocation
zeroes new clusters and rolls back a partial allocation when space runs out.
The directory walker processes records in disk order, including long names
crossing cluster boundaries. Long-name order and checksum must agree with the
following short record; invalid or unsupported names fall back to their 8.3
alias. Lookup is case-insensitive for ASCII.

The public name buffer supports up to 127 ASCII bytes. Creation validates names,
generates unique 8.3 aliases when needed, and extends directory chains. Full
Unicode/255-character FAT names remain unsupported. Each open entry records its
actual directory locations so updates and deletion touch the right records.
Directory deletion requires an empty directory; it does not recursively follow
potentially corrupt directory graphs.

## Persistence and limits

Data and directory changes are written through the block interface. Allocation
changes flush the cached FAT to the configured active/mirrored copies and mark
FSInfo free-space hints unknown. The destructor flushes any remaining dirty FAT;
read-only mounts do not rewrite boot sectors. Host mutation tests save the image
and read it with mtools to check the persisted representation independently.

There is no journal, transaction rollback after device failure, or device cache
flush command. `Device::Read` and `Write` return `void`, so hardware errors still
cannot reach FAT32 or VFS. Open handles also cache metadata; concurrent opens,
writes and deletion need explicit ownership and synchronization rules.

Use disposable images for write experiments. The normal Docker/QEMU runner
uses a snapshot overlay, so its guest writes do not persist after shutdown.
See [testing](../development/testing.md) and [TODO](../TODO.md).
