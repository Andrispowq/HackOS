# Filesystems

[System index](index.md) · [Storage devices](devices.md) · [User ABI](userspace.md)

File access passes through the VFS or the process-owned syscall handles, the
`Filesystem`/`ActiveFile` interfaces, the FAT32 driver, and a journal overlay over
its block `Device`. The account store retains its separate two-slot protocol.

## Mounting and paths

BIOS discovery still reads the relocated boot MBR at `0x0600`; UEFI assumes a
volume at LBA 0. Device/partition selection remains a separate backlog item.
The BPB is checked against the device's reported 512-byte sector capacity before
journal recovery or allocation. Sector size, FAT capacity, cluster size/count,
root cluster, and active/mirrored FAT selection are validated.

Images require the version-1 HackOS storage extension in 512 FAT reserved
sectors. Earlier images and ordinary FAT volumes fail closed; they are not
silently assigned public ownership. Rebuild to create the new format. The
existing account-preserving rebuild can copy accounts from an older image;
other guest files and permission changes are not preserved by a rebuild.
The BIOS partition start and boot payload reservations are unchanged.

Names are bounded ASCII, case-insensitive, and paths are canonicalized before
userspace access checks. Long names have at most 127 bytes; invalid LFN order or
checksum falls back to the short record. Chains are range/cycle checked. There
are no links, recursive deletion, directory moves, or cross-volume user moves.

## Ownership and access

FAT attributes remain separate from permissions. Each ordinary directory entry
has a mandatory record keyed by its physical short-entry cluster and slot;
root has key `(0, 0)`. A record stores UID, GID and nine owner/group/other `rwx`
bits. Missing, corrupt or duplicate matching metadata denies access. Creation,
rename, deletion and ownership changes update these records in the same journal
transaction as the FAT/directory changes. Replacing file contents preserves its
ownership. Deletion clears the record before its slot can be reused.

Packaged files belong to UID/GID 0. Directories and `/usr/bin` executables have
mode 0755, other files 0644. `/tmp` is 0777. New files are 0644 and new directories
0755, owned by the creating process's UID and primary GID. There is no umask,
sticky bit, setuid/setgid, ACL or per-user home creation. In particular, `/tmp`
is shared: its directory permissions allow users to remove each other's files.

The kernel checks search (`x`) on ancestors, `r` to read files, `w` to replace or
open for writing, and `r+x` to enumerate directories or load programs. `chdir`
requires search. Create/unlink/rename require `w+x` on the relevant parent(s).
`stat` requires ancestor search. The owner's class wins over group/other;
primary and supplementary groups participate in group checks. Root bypasses
access bits except that a program must have at least one execute bit. A file
with the FAT read-only flag rejects content changes, resizing, removal and
rename even for root; changing its mode does not clear that FAT flag.

`chmod <octal-mode> <path>` requires the owner or root; `chown <user> <group>
<path>` requires root and registered IDs. Root mode is fixed at 0755 through the
user interface. Account payloads remain inaccessible to all user programs and
`/SYSTEM` mutations remain forbidden. Mode/ownership changes and path mutations
reject live handles, so an open descriptor's metadata never changes underneath
it. `ls -l` shows the actual mode, numeric UID/GID, FAT attributes and size.

## Handles and checked I/O

Userspace gets eight process-owned, nontransferable handles. `open` remains
read-only. `open_mode` adds read/write/create flags; readers can share, while a
writer excludes every other opener. `file_write` accepts at most 512 bytes,
`seek` is absolute and bounded to 32 KiB, and growth zero-fills gaps. Unknown
flags, foreign/stale handles and invalid buffers fail before mutation. Handle
creation reserves capacity before creating a file. Exit/kill/fault reap handles.
Each write commits before success; `flush` is an explicit durability/error
boundary. Closing a writer flushes and releases it even if flushing fails.

The internal `fsN:/...` VFS also uses bounded opaque tokens, rejects unknown
modes and stale/arbitrary tokens, enforces `O_READ`/`O_WRITE`, and requires write
access for `O_CREAT`. It creates only on an explicit missing result, not after
an I/O failure. `fread`/`fwrite`, checked resize/delete, `fflush` and `fclose`
return status (zero success, negative failure), rather than stdio byte counts.
Direct kernel `ActiveFile` users must still manage object lifetime; these
internal interfaces are not exposed as pointers to userspace. DirectoryCount
separates enumeration failure from an empty directory.

## Journal and flush contract

Every FAT mutation, including the older resize/delete/descriptor paths, runs in
one bounded sector overlay. Reads see staged sectors; unchanged FAT sectors do
not consume journal capacity. Failed validation, full media, a full permission
table, or exceeding 256 changed sectors discards the overlay and reloads the FAT.
No staged changes reach home sectors. Whole-file saves remain bounded to 32 KiB.

Commit writes the address table and full new sector contents, flushes, writes a
checksummed prepared header, flushes, replays the home sectors, flushes, then
writes a clean header and flushes again. Success therefore means both data and
metadata reached the device's declared flush boundary. An I/O or flush failure
freezes the mount. A reported failure after publication may still commit on
reboot; callers must inspect the file after recovery rather than assume rollback.

Mount validates a prepared journal's header, payload hash, unique targets and
volume bounds before replaying anything. Replay is idempotent and completes
before loading the FAT or permissions. Interruption before publication leaves
the old filesystem; interruption afterwards replays the complete new state.
Rename cannot leave two names sharing a chain after successful recovery. This
contract relies on honest device completion/flush results. A torn or corrupted
journal header/payload fails closed and requires offline recovery; the system
does not promise repair of arbitrary corruption or external filesystem edits.

### Version-1 layout

All locations below are relative to the FAT volume and all integers are little
endian. The 48,000,000-byte data partition remains the same size.

| Sector(s) | Contents |
| --- | --- |
| 0–7 | Standard boot sector, FSInfo and backups |
| 8 | 512-byte journal header |
| 9–12 | 256 absolute 64-bit target LBAs; unused addresses zero |
| 13–268 | Up to 256 complete replacement sectors |
| 269–271 | Reserved |
| 272–399 | 2048 fixed 32-byte permission records, including root |
| 400–511 | Reserved |

The header has magic `HKFSJ016`, uint32 version 1 and count, a 32-byte SHA-256
of the entire address table plus `count` payload sectors, then a 32-byte SHA-256
of the header with only its own checksum zeroed. Remaining header bytes are
zero. Count zero denotes clean, with a zero payload digest. Targets may be
FSInfo sectors 1/7, permission sectors, or FAT/data sectors; never the boot or
journal sectors.

Permission records contain eight uint32 fields: tag `0x36315048`, parent
cluster, slot, UID, GID, mode, zero reserved, and FNV-1a over the first 28 bytes.
An unused record is entirely zero. Checksums detect corruption, not malicious
offline alteration; this is not encryption or authenticated storage.

`tools/storage_image.py` provisions fresh images and controlled test imports.
To support older mtools, it formats a smaller volume with 32 reserved sectors,
inserts 480 sectors before the FATs and updates both BPBs without changing
cluster indices. Ordinary mtools reads remain useful for independent checking.
External writes can invalidate ownership keys or bypass recovery and are not a
supported way to edit a used HackOS filesystem. Never clear a dirty journal or
reinitialize metadata to hide corruption. Use disposable images for tests.
