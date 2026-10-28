"""Separate directory enumeration, small writes and grouped output on a disposable image."""
from pathlib import Path
import argparse, json, re, shutil, time
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD
from qemu_shell import build_probe
from session import provision_imports

def measure(firmware, build_directory=None):
    directory = OUT / ('qemu-listing-performance-' + firmware)
    directory.mkdir(exist_ok=True)
    built = build_directory or OUT / ('build-' + firmware)
    probe = build_probe(directory, built/'source', extra=('listing_probe.cpp', 'LSPROBE'))
    image = directory/'HackOS.img'
    shutil.copy2(built/'HackOS.img', image)
    volume = str(image) + ('@@433152' if firmware == 'bios' else '')
    require_success(command(['mcopy', '-o', '-i', volume, probe, '::/USR/BIN/LSPROBE']), 'install listing probe')
    provision_imports(image, firmware)
    results = []
    with boot(directory, image, firmware, 'listing', 180) as guest:
        guest.fresh = True
        guest.setup()
        guest.enter('admin', 'New password: ')
        guest.password(ADMIN_PASSWORD, 'Action: ')
        guest.login('admin', ADMIN_PASSWORD)
        for _ in range(3):
            offset = len(guest.output())
            guest.shell('LSPROBE', 'LISTING_PROBE_OK')
            data = {key: int(value) for key, value in re.findall(r'(LIST_ENTRIES|ENUM_TICKS|SMALL_WRITE_TICKS|GROUPED_WRITE_TICKS)=(\d+)', guest.output()[offset:])}
            guest.qmp.type('ls /USR/BIN')
            offset = len(guest.output())
            start = time.monotonic()
            guest.qmp.key('ret')
            guest.wait('admin@root:~$ ', offset)
            data['ls_seconds'] = time.monotonic() - start
            results.append(data)
    (directory/'timings.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios','uefi'), required=True)
    parser.add_argument('--build-directory', type=Path)
    args = parser.parse_args()
    measure(args.firmware, args.build_directory)
