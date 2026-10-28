#!/usr/bin/env python3
"""Boot large fragmented kernels and require explicit rejection of malformed inputs."""
import argparse
import shutil
import struct
import subprocess
import sys
import time
from common import ROOT, OUT, Report, command, require_success
from qemu_boot import boot


class Fat:
    def __init__(self, image, firmware):
        self.data = bytearray(image.read_bytes())
        self.base = 846 * 512 if firmware == 'bios' else 0
        b = self.base
        self.spc = self.data[b + 13]
        self.reserved = struct.unpack_from('<H', self.data, b + 14)[0]
        self.fats = self.data[b + 16]
        self.fatsize = self.u32(b + 36)
        self.fat = b + self.reserved * 512
        self.start = self.fat + self.fats * self.fatsize * 512
        self.clusters = (self.u32(b + 32) - self.reserved - self.fats * self.fatsize) // self.spc
        root = self.u32(b + 44)
        self.entry = None
        for cluster in self.chain(root):
            start = self.address(cluster)
            for off in range(start, start + self.spc * 512, 32):
                if self.data[off:off + 11] == b'KERNEL  ELF':
                    self.entry = off
                    break
        assert self.entry is not None
        self.first = (struct.unpack_from('<H', self.data, self.entry + 20)[0] << 16) | struct.unpack_from('<H', self.data, self.entry + 26)[0]

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]

    def set32(self, offset, value):
        struct.pack_into('<I', self.data, offset, value)

    def address(self, cluster):
        return self.start + (cluster - 2) * self.spc * 512

    def chain(self, first):
        seen = set()
        while first < 0x0ffffff8:
            assert 2 <= first < self.clusters + 2 and first not in seen
            seen.add(first)
            yield first
            first = self.u32(self.fat + first * 4) & 0x0fffffff

    def link(self, cluster, value):
        for index in range(self.fats):
            self.set32(self.fat + index * self.fatsize * 512 + cluster * 4, value)

    def replace(self, payload):
        old = list(self.chain(self.first))
        available = [c for c in range(2, self.clusters + 2) if not self.u32(self.fat + c * 4)]
        size = self.spc * 512
        chain = available[::2][: (len(payload) + size - 1) // size]
        assert len(chain) * size >= len(payload)
        assert all(b != a + 1 for a, b in zip(chain, chain[1:])), 'fixture must be fragmented'
        for i, c in enumerate(chain):
            self.link(c, chain[i + 1] if i + 1 < len(chain) else 0x0fffffff)
            part = payload[i * size:(i + 1) * size]
            self.data[self.address(c):self.address(c) + size] = part.ljust(size, b'\0')
        for c in old:
            self.link(c, 0)
        struct.pack_into('<H', self.data, self.entry + 20, chain[0] >> 16)
        struct.pack_into('<H', self.data, self.entry + 26, chain[0] & 65535)
        self.set32(self.entry + 28, len(payload))
        self.first = chain[0]


def enlarged(data):
    data = bytearray(data)
    phoff = struct.unpack_from('<Q', data, 32)[0]
    count = struct.unpack_from('<H', data, 56)[0]
    # Extra loaded data crosses the old whole-file limit and the old 2 MiB reservation.
    offset = (len(data) + 4095) & ~4095
    payload = bytes(range(256)) * 2048
    data.extend(bytes(offset - len(data)))
    data.extend(payload)
    struct.pack_into('<IIQQQQQQ', data, phoff + count * 56,
                     1, 6, offset, 0x200000, 0x200000, len(payload), len(payload) + 65536, 4096)
    struct.pack_into('<H', data, 56, count + 1)
    return data


def rejected(firmware, image, directory, expected):
    serial = directory / 'serial.log'
    args = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg', '-cpu', 'qemu64,+rdrand',
            '-m', '1G', '-smp', '4', '-display', 'none', '-monitor', 'none', '-net', 'none',
            '-serial', 'file:' + str(serial), '-no-reboot', '-snapshot',
            '-drive', f'file={image},format=raw', '-usb', '-device', 'usb-ehci,id=ehci', '-device', 'qemu-xhci,id=xhci']
    if firmware == 'uefi':
        ovmf = ROOT / 'boot/UEFI/OVMFbin'
        variables = directory / 'vars.fd'
        shutil.copy2(ovmf / 'OVMF_VARS-pure-efi.fd', variables)
        args += ['-drive', f'if=pflash,format=raw,unit=0,file={ovmf / "OVMF_CODE-pure-efi.fd"},readonly=on',
                 '-drive', f'if=pflash,format=raw,unit=1,file={variables}']
    with (directory / 'qemu.log').open('w') as log:
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                output = serial.read_text(errors='replace') if serial.exists() else ''
                assert 'HackOS kernel' not in output
                if expected in output:
                    return
                assert process.poll() is None, 'QEMU exited before explicit loader failure'
                time.sleep(0.1)
            raise AssertionError('No explicit loader rejection: ' + output)
        finally:
            process.terminate()
            process.wait(timeout=5)


def main(firmware):
    built = OUT / ('build-' + firmware)
    directory = OUT / ('qemu-loader-' + firmware)
    report = Report('qemu-loader-' + firmware, directory)
    original = built / 'HackOS.img'
    kernel = (built / 'source/bin/kernel.elf').read_bytes()
    def large():
        fs = Fat(original, firmware)
        fs.replace(enlarged(kernel))
        image = directory / 'large.img'
        image.write_bytes(fs.data)
        elf = directory / 'large.elf'
        elf.write_bytes(enlarged((built / 'kernel.elf').read_bytes()))
        assert boot(firmware, image, elf, timeout=120,
                    report_name='qemu-loader-' + firmware + '/large') == 0
    report.check('large_fragmented_kernel', large)
    def invalid(name, mutation, missing=False):
        def run():
            case = directory / name
            case.mkdir(exist_ok=True)
            fs = Fat(original, firmware)
            mutation(fs)
            image = case / 'image.img'
            image.write_bytes(fs.data)
            expected = ('BIOS: invalid FAT32' if firmware == 'bios' else
                        'UEFI: Cannot open' if missing else 'UEFI: Cannot load kernel')
            rejected(firmware, image, case, expected)
        report.check(name, run)
    invalid('missing', lambda f: f.data.__setitem__(f.entry, 0xe5), True)
    invalid('bad_magic', lambda f: f.data.__setitem__(f.address(f.first), 0))
    invalid('segment_file_overflow', lambda f: struct.pack_into('<Q', f.data, f.address(f.first) + 64 + 32, 0xffffffffffffffff))
    invalid('segment_overlap', lambda f: struct.pack_into('<QQ', f.data, f.address(f.first) + 120 + 16, 0x100000, 0x100000))
    invalid('unavailable_destination_ram', lambda f: struct.pack_into('<QQ', f.data, f.address(f.first) + 120 + 16, 0x60000000, 0x60000000))
    if firmware == 'bios':
        invalid('cyclic_chain', lambda f: f.link(list(f.chain(f.first))[-1], f.first))
        invalid('truncated_chain', lambda f: f.link(f.first, 0x0fffffff))
        invalid('partition_bound', lambda f: f.set32(f.base + 32, 93751))
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=('bios', 'uefi'))
    sys.exit(main(parser.parse_args().firmware))
