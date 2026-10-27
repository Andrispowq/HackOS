#!/usr/bin/env python3
"""Package boot payloads without truncating them or mounting a filesystem."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from storage_image import provision, format_volume

ROOT = Path(__file__).resolve().parents[1]

def package(firmware):
    output = ROOT / 'bin'
    output.mkdir(exist_ok=True)
    slots = [(0, 512, 'bin/mbr.bin'), (1, 512, 'bin/bootloader.bin'),
             (2, 32768, 'bin/second_stage.bin'), (66, 6144, 'util/fonts/zap-light16.psf'),
             (78, 393216, 'bin/kernel.elf')]
    if firmware == 'bios':
        for _, limit, name in slots:
            size = (ROOT / name).stat().st_size
            if not 0 < size <= limit:
                raise ValueError(f'{name}: {size} bytes exceeds its {limit}-byte BIOS slot')
    def mtool(command, *args):
        subprocess.run([command, '-i', str(fat), *map(str, args)], check=True)
    with tempfile.TemporaryDirectory(prefix='image-', dir=output) as temporary:
        fat = Path(temporary) / 'fat.img'
        format_volume(fat)
        for name in ('TMP', 'SYSTEM', 'USR', 'USR/BIN', 'UTIL', 'UTIL/FONTS'):
            mtool('mmd', '::/' + name)
        for source, dest in [('util/fonts/zap-light16.psf', 'UTIL/FONTS/zap-light16.psf'),
                             ('util/usertest/usertest.elf', 'USR/BIN/usertest.elf'),
                             ('libs/test/bin/test', 'USR/BIN/test'),
                             ('libs/input/bin/input', 'USR/BIN/input'),
                             ('libs/window/bin/window', 'USR/BIN/window'),
                             ('libs/ring3/bin/ring3', 'USR/BIN/ring3'),
                             ('libs/shell/bin/shell', 'USR/BIN/shell'), ('libs/login/bin/login', 'USR/BIN/login'), ('hello.txt', 'USR/hello.txt')]:
            mtool('mcopy', ROOT / source, '::/' + dest)
        for name in ('help', 'clear', 'terminal', 'echo', 'time', 'sysinfo', 'ps', 'kill', 'ls', 'wd', 'cat', 'read', 'grep', 'wc', 'head', 'tail', 'tee', 'more', 'shutdown', 'restart', 'id', 'whoami', 'users', 'groups', 'useradd', 'groupadd', 'usermod', 'runas', 'passwd', 'xxd', 'mkdir', 'rm', 'mv', 'edit', 'chmod', 'chown'):
            mtool('mcopy', ROOT / 'libs/commands/bin' / name, '::/USR/BIN/' + name)
        empty_accounts = Path(temporary) / 'accounts.dat'
        empty_accounts.write_bytes(bytes(8192))
        for slot in (0, 1):
            mtool('mcopy', empty_accounts, f'::/SYSTEM/ACCT{slot}.DAT')
            mtool('mattrib', '+h', '+s', f'::/SYSTEM/ACCT{slot}.DAT')
        image = Path(temporary) / 'HackOS.img'
        if firmware == 'uefi':
            mtool('mmd', '::/EFI', '::/EFI/BOOT')
            efi = ROOT / 'boot/UEFI/gnu-efi/x86_64/bootloader/main.efi'
            mtool('mcopy', efi, '::/EFI/BOOT/BOOTX64.EFI')
            mtool('mcopy', ROOT / 'kernel/startup.nsh', '::/startup.nsh')
            mtool('mcopy', ROOT / 'bin/kernel.elf', '::/kernel.elf')
            fat.rename(image)
        else:
            with image.open('wb') as f:
                f.truncate((846 + 93750) * 512)
                for lba, _, name in slots:
                    f.seek(lba * 512)
                    f.write((ROOT / name).read_bytes())
                f.seek(846 * 512)
                with fat.open('rb') as source:
                    while data := source.read(1024 * 1024):
                        f.write(data)
        provision(image, 846 * 512 if firmware == 'bios' else 0, initialize=True)
        image.replace(output / 'HackOS.img')
    print(f'Built {output / "HackOS.img"} ({firmware})')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=('bios', 'uefi'))
    package(parser.parse_args().firmware)
