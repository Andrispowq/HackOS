#!/usr/bin/env python3
"""Exercise the root-only visible network report and its copied ABI in a real guest."""
import argparse
from pathlib import Path
import shutil
import sys
from common import ROOT, OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, image, timeout):
    directory = OUT / ('network-demo-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    probe = build_probe(directory, image.parent / 'source', extra=('utilities_probe.cpp', 'UTILPROBE'))
    require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/UTILPROBE']), 'install ABI probe')
    provision_imports(disposable, firmware)
    with boot(directory, disposable, firmware, 'demo', timeout, qmp_stdio=True, network=True) as guest:
        guest.fresh = True
        guest.setup()
        guest.enter('root', 'New password: ')
        guest.password(ADMIN_PASSWORD, 'Action: ')
        guest.login('root', ADMIN_PASSWORD)
        guest.shell('utilprobe', 'UTILITIES_ABI_OK')
        guest.shell('runas guest utilprobe', 'UTILITIES_DENIED_OK')
        guest.shell('runas guest sysinfo network', 'root privileges required.')
        guest.shell('clear', '$ ')
        start = len(guest.output())
        guest.shell('sysinfo network', 'Ethernet + ARP only.')
        text = guest.output()[start:]
        assert 'Intel E1000 (82540EM)' in text and 'resolved' in text, text
        assert '52:54:00:12:34:56' in text and '52:55:0a:00:02:02' in text, text
        assert '10.0.2.15' in text and '10.0.2.2' in text, text
        assert 'TX frames queued' in text and 'RX frames received' in text, text
        guest.screen('network-report')
        guest.shell('sysinfo network > /tmp/network.txt', '$ ')
        guest.shell('cat /tmp/network.txt', 'Gateway MAC')
    print(f'PASS {firmware}: visible network report, ABI bounds, root authorization and redirection')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout)
