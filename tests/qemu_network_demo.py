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
    probe = build_probe(directory, image.parent / 'source', extra=('ping_probe.cpp', 'PINGPROBE'))
    require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/PINGPROBE']), 'install ping probe')
    provision_imports(disposable, firmware)
    with boot(directory, disposable, firmware, 'demo', timeout, qmp_stdio=True, network=True, capture=directory / 'ping.pcap') as guest:
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
        guest.shell('sysinfo network', 'Ethernet + ARP + IPv4/ICMP.')
        text = guest.output()[start:]
        assert 'Intel E1000 (82540EM)' in text and 'resolved' in text, text
        assert '52:54:00:12:34:56' in text and '52:55:0a:00:02:02' in text, text
        assert '10.0.2.15' in text and '10.0.2.2' in text, text
        assert 'TX frames queued' in text and 'RX frames received' in text, text
        guest.screen('network-report')
        guest.shell('sysinfo network > /tmp/network.txt', '$ ')
        guest.shell('cat /tmp/network.txt', 'Gateway MAC')
        guest.shell('ping --help', 'Usage: ping')
        guest.shell('ping -c 0 10.0.2.2', 'Usage: ping')
        guest.shell('ping -c 11 10.0.2.2', 'Usage: ping')
        guest.shell('ping 1.1.1.1', 'Usage: ping')
        guest.shell('pingprobe', 'PING_ABI_OK')
        guest.shell('runas guest pingprobe', 'PING_ABI_OK')
        guest.shell('clear', '$ ')
        guest.shell('ping 10.0.2.2', '4 probes, 4 received, 0% loss')
        guest.screen('ping')
        guest.shell('runas guest ping -c 2 10.0.2.2', '2 probes, 2 received, 0% loss')
        guest.shell('ping -c 1 10.0.2.2 > /tmp/ping.txt', '$ ')
        guest.shell('cat /tmp/ping.txt', '1 probes, 1 received, 0% loss')
    from qemu_network import packets
    def checksum(data):
        if len(data) % 2:
            data += b'\0'
        total = sum(int.from_bytes(data[i:i+2], 'big') for i in range(0, len(data), 2))
        while total >> 16:
            total = (total & 65535) + (total >> 16)
        return (~total) & 65535
    requests, replies = set(), set()
    for frame in packets(directory / 'ping.pcap'):
        if len(frame) < 74 or frame[12:14] != b'\x08\0' or frame[23] != 1:
            continue
        assert frame[14] == 0x45 and int.from_bytes(frame[16:18], 'big') == 60
        assert checksum(frame[14:34]) == 0 and checksum(frame[34:74]) == 0
        key = frame[38:74]
        if frame[34] == 8:
            assert frame[26:30] == bytes([10, 0, 2, 15]) and frame[30:34] == bytes([10, 0, 2, 2])
            requests.add(key)
        elif frame[34] == 0:
            replies.add(key)
    assert len(requests & replies) >= 7, 'Missing real ICMP exchanges in packet capture'
    with boot(directory, disposable, firmware, 'no-nic', timeout, qmp_stdio=True) as guest:
        guest.login('root', ADMIN_PASSWORD)
        guest.shell('ping -c 1 10.0.2.2', 'network unavailable')
    print(f'PASS {firmware}: visible network report, ABI bounds, root authorization, ping, ownership, cleanup, no-NIC and redirection')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout)
