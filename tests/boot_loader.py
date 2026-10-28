#!/usr/bin/env python3
"""Exercise production boot FAT32 and shared ELF validation with malformed media."""
import os
import struct
import sys
from common import ROOT, OUT, Report, command, require_success


def main():
    directory = OUT / 'boot-loader'
    report = Report('boot-loader', directory)
    runner = directory / 'checks'
    def compile_checks():
        runner.unlink(missing_ok=True)
        require_success(command([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-O1',
                                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', ROOT,
                                 ROOT / 'tests/boot/loader_checks.c',
                                 ROOT / 'boot/BIOS/stage_2/src/fat32/boot_fat.c', '-o', runner]), 'compile boot checks')
        require_success(command([runner]), 'ELF validation checks')
    report.check('shared_elf', compile_checks)
    if not runner.exists():
        return report.finish()
    partition, fats, count = 846, 547, 70000
    total = 1 + fats + count
    base = bytearray((partition + total) * 512)
    def u16(data, offset, value):
        struct.pack_into('<H', data, offset, value)
    def u32(data, offset, value):
        struct.pack_into('<I', data, offset, value)
    u16(base, 510, 0xaa55)
    base[466] = 0x0b  # FAT partition is separate from the active loader partition.
    u32(base, 470, partition)
    u32(base, 474, total)
    bpb = partition * 512
    u16(base, bpb + 11, 512)
    base[bpb + 13] = 1
    u16(base, bpb + 14, 1)
    base[bpb + 16] = 1
    u32(base, bpb + 32, total)
    u32(base, bpb + 36, fats)
    u32(base, bpb + 44, 2)
    u16(base, bpb + 510, 0xaa55)
    fat = (partition + 1) * 512
    data = (partition + 1 + fats) * 512
    u32(base, fat + 8, 4)
    u32(base, fat + 16, 0x0fffffff)
    # A root directory spanning two nonadjacent clusters.
    for offset in range(data, data + 512, 32):
        base[offset] = 0xe5
    entry = data + 2 * 512
    base[entry:entry + 11] = b'KERNEL  ELF'
    base[entry + 11] = 0x20
    payload = bytes((i * 13 + i // 511) % 256 for i in range(600000))
    expected = directory / 'expected'
    expected.write_bytes(payload)
    chain = [100 + 2 * i for i in range((len(payload) + 511) // 512)]
    u16(base, entry + 26, chain[0])
    u32(base, entry + 28, len(payload))
    for i, cluster in enumerate(chain):
        u32(base, fat + cluster * 4, chain[i + 1] if i + 1 < len(chain) else 0x0fffffff)
        start = data + (cluster - 2) * 512
        part = payload[i * 512:(i + 1) * 512]
        base[start:start + len(part)] = part
    image = directory / 'fixture.img'
    def check(name, mutation=None, valid=False, failure=None):
        def run():
            media = bytearray(base)
            if mutation:
                mutation(media)
            image.write_bytes(media)
            require_success(command([runner, image, expected if valid else '-',
                                     *([str(failure)] if failure else [])]), name)
        report.check(name, run)
    check('fragmented_large_file_and_directory', valid=True)
    check('missing', lambda x: x.__setitem__(entry, 0))
    check('truncated_chain', lambda x: u32(x, fat + chain[0] * 4, 0x0fffffff))
    check('cyclic_file', lambda x: u32(x, fat + chain[-1] * 4, chain[0]))
    check('cyclic_root', lambda x: u32(x, fat + 16, 2))
    check('out_of_range_cluster', lambda x: u32(x, fat + chain[0] * 4, count + 2))
    check('reserved_cluster', lambda x: u32(x, fat + chain[0] * 4, 0x0ffffff0))
    check('bad_cluster', lambda x: u32(x, fat + chain[0] * 4, 0x0ffffff7))
    check('partition_extent', lambda x: u32(x, 474, total - 1))
    check('partition_overflow', lambda x: u32(x, 470, 0xfffffff0))
    check('fat_too_short', lambda x: u32(x, bpb + 36, 1))
    check('invalid_cluster_size', lambda x: x.__setitem__(bpb + 13, 3))
    check('invalid_active_fat', lambda x: u16(x, bpb + 40, 0x81))
    check('disk_failure', failure=3)
    return report.finish()


if __name__ == '__main__':
    sys.exit(main())
