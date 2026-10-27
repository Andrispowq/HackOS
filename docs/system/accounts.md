# Persistent accounts and login

[System index](index.md) · [Userspace ABI](userspace.md) · [Build and run](../development/building.md)

The boot program is `USR/BIN/LOGIN`. It has the reserved UID/GID `0xffffffff`,
no root rights, and no ordinary spawn rights. Only this kernel-designated agent
can query authentication state, set up the first administrator or authenticate
and create a session. Running another copy of LOGIN does not grant these rights.
There is no automatic root shell or default password.

## Setup and daily use

At boot and after logout, the console offers `1) login`, `2) restart` and
`3) shutdown`. Enter a number or action name (case-insensitive). Invalid input
returns to the menu. Restart boots the same image again in the normal runner.
Login starts setup on a new installation; restart and
shutdown also work before setup or when account storage is damaged.

Two completely zeroed account files identify a new installation. Setup asks for
a username matching `[a-z][a-z0-9_-]*` (1–31 characters) and a confirmed password
of 8–128 printable ASCII characters. The chosen account receives UID/GID 0;
`root` is a valid name but is not required. The reserved `guest` account has
UID/GID 1000 and starts locked. Setup saves the account, then requires login.

Root can run `groupadd staff`, `useradd alice staff`, `usermod alice users`, and
`passwd alice`. New accounts are locked until root sets a password. `passwd`
without a name changes the calling administrator's password. Password changes
are root-only in this version. `runas` remains a root administrative launcher
and can use locked identities; it does not authenticate a password.

Passwords and confirmations are never echoed or added to shell history, including
the debug console. Backspace edits hidden input; overflow and Ctrl-C discard the
value. Kernel and userspace password buffers are explicitly erased after use.
Failed logins use a generic error and a one-second retry delay. Unknown or locked
accounts run the same password KDF as enabled accounts. There is no permanent
lockout or account enumeration through authentication errors.

Successful authentication loads a fresh shell with a copied credential snapshot.
The shell's PID identifies its session, inherited by all descendants, including
`runas` children. `logout`, shell exit or shell fault terminates the entire session;
idle reclaims private pages, stacks, file handles and windows. The login agent
waits for the shell, clears the console and returns to the action menu. Neither
the agent nor the authenticated shell can be killed through the user kill API.
Account administration requires UID 0; membership in group 0 does not grant it.
Power operations require UID 0, except for the kernel-designated boot login
agent while it owns the logged-out terminal. Ordinary non-root processes,
including manually launched copies of LOGIN, retain no power rights.

## Password representation

Each enabled password stores a 16-byte random salt, a 32-byte PBKDF2-HMAC-SHA256
derived value and a fixed 600,000 iteration count. SHA-256/HMAC/PBKDF2 live in
`kernel/src/security/crypto.cpp`; host vectors and Python's independent
`hashlib.pbkdf2_hmac` verify them. Long passwords are prehashed once for HMAC.
Comparison accumulates all digest differences. This follows the PBKDF2 structure
in [RFC 8018](https://www.rfc-editor.org/rfc/rfc8018.html) and the SHA-256 work
factor in [OWASP's password storage guidance](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).

Salts require CPUID-advertised RDRAND with carry-flag success, retried at most ten
times per 64-bit word, following [Intel's DRNG guidance](https://www.intel.com/content/www/us/en/developer/articles/guide/intel-digital-random-number-generator-drng-software-implementation-guide.html).
No timestamp or predictable fallback exists. QEMU launchers enable `+rdrand`;
unsupported physical CPUs cannot create/change passwords, but can verify an
existing store. Hashing is synchronous inside the syscall preemption guard, so
other tasks pause during it; the 600,000 rounds are noticeable under TCG.

## Disk format and commit contract

The image contains hidden/system files `SYSTEM/ACCT0.DAT` and `ACCT1.DAT`, each
8192 bytes. Their validated FAT chains map to separate sets of sixteen sectors.
The kernel overwrites only these preallocated data sectors, without changing
FAT entries or directory metadata during account updates. Generic user path
operations cannot open these files, including from root programs.

All fields are little-endian with explicitly asserted fixed sizes:

| Offset | Contents |
| ---: | --- |
| 0 | 8-byte magic `HOSACCT1` |
| 8 | Version 1 and payload length 5648, each uint32 |
| 16 | Monotonically increasing uint64 generation, starting at 1 |
| 24 | SHA-256 of all 8192 bytes with these 32 digest bytes zeroed |
| 56 | Zero uint64 reserved |
| 64 | Counts/next IDs (16 bytes), 32 user records (80 bytes each), 32 group records (40 bytes each), 32 password records (56 bytes each) |
| 5712 | Zero padding to 8192 bytes |

Password records contain uint32 enabled/iterations, salt[16], digest[32]. Disabled
records are zero. Registry validation checks names, counts, unique IDs, group
references and the supported KDF cost before accepting a generation.

A commit writes the inactive slot, checks the device result, flushes its cache,
reads the complete slot back and compares it. Only then does the new registry
become committed. Failure rolls the in-memory registry back and freezes further
authentication/account changes until reboot; a failed flush has an uncertain
on-disk outcome. Existing sessions retain their credentials until they end.
ATA PIO and AHCI implement checked I/O and cache flushes. AHCI uses a bounded,
physically contiguous bounce page and stops reusing it after a command timeout.
This is a narrow account-storage contract, not a journaled general FAT writer.

On boot the newest valid generation wins. A damaged generation can fall back to
the other valid one, potentially reverting the most recent password/account
change. Both all-zero files permit setup. Missing files, invalid sizes/chains,
I/O errors, two damaged generations, conflicting equal generations, or a torn
initial save with no valid generation disable login. Corruption never silently
reopens setup.

## Persistence and recovery

The normal runner saves to its disk image. `--snapshot` intentionally discards
changes. Docker rebuilds preserve both account files in the named volume;
`docker compose down --volumes` removes them. Direct image packaging produces
an empty store; back up the existing image or use `tools/build.py
--preserve-accounts` before rebuilding. BIOS and UEFI images have separate stores.

For recovery, stop QEMU and keep a copy of the whole affected image first. Restore
both account files from a matching backup using host mtools or
`tools/account_image.py`'s `read_accounts`/`write_accounts` helpers. These helpers
operate only on image files and preserve opaque bytes, including damaged records.
Do not zero one damaged file to bypass the fail-closed rule. If there is no valid
backup, deliberately create a fresh image and run setup; accounts and passwords
cannot be recovered from missing/corrupt data. Pre-step-14 images have no store
and must be moved aside before Docker can create a fresh one.

This provides local login and kernel-enforced process privileges. FAT32 still
has no general per-file UID/GID/mode metadata. Checksums detect accidental damage;
they are not keyed authentication. Offline image editors can replace the kernel,
login program or account store, and rollback to an older valid image is possible.
There is no disk encryption, secure boot, trusted recovery console or remote
login service.
