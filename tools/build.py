#!/usr/bin/env python3
"""Build in a fresh source copy, leaving the checkout and binary inputs untouched."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def build(firmware, output):
    directory = output.resolve() / firmware
    directory.mkdir(parents=True, exist_ok=True)
    for name in ('HackOS.img', 'kernel.elf'):
        (directory / name).unlink(missing_ok=True)
    work = directory / 'source'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    def ignore(folder, names):
        ignored = set(names) & {'bin', 'bin-int', '__pycache__', '.DS_Store'}
        if Path(folder) == ROOT / 'boot/UEFI/gnu-efi': ignored.add('x86_64')
        return ignored
    for name in ('boot', 'kernel', 'libs', 'tools'):
        shutil.copytree(ROOT / name, work / name, ignore=ignore)
    for name in ('Makefile', 'hello.txt', 'util/fonts/zap-light16.psf', 'util/usertest/usertest.elf'):
        target = work / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    env = os.environ.copy()
    env.pop('MAKEFLAGS', None)
    cross = env.get('CROSS_COMPILE', 'x86_64-elf-')
    versions = []
    for tool in (cross+'gcc', cross+'ld', cross+'objcopy', 'nasm', 'mformat', 'qemu-system-x86_64'):
        result = subprocess.run([tool, '-V' if tool == 'mformat' else '--version'], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        versions.append(result.stdout)
    (directory / 'toolchain.txt').write_text('\n'.join(versions))
    target = 'build_bios' if firmware == 'bios' else 'build_efi'
    subprocess.run(['make', '-j2', target, 'CROSS_COMPILE='+cross], cwd=work, env=env, check=True)
    shutil.copy2(work / 'bin/HackOS.img', directory / 'HackOS.img')
    shutil.copy2(work / 'kernel/bin/kernel.elf', directory / 'kernel.elf')
    if firmware == 'uefi': shutil.copy2(work / 'boot/UEFI/gnu-efi/x86_64/bootloader/main.efi', directory / 'BOOTX64.EFI')
    print(f'Image and debug symbols: {directory}', flush=True)
    return directory / 'HackOS.img'

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--output', type=Path, default=ROOT / 'out')
    args = parser.parse_args()
    build(args.firmware, args.output)
