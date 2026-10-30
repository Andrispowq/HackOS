#!/usr/bin/env python3
"""Sanitize the real Ethernet/ARP/IPv4/ICMP/UDP/DNS and request lifecycles with malformed and truncated frames."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitizers', default='address,undefined', choices=('address,undefined', 'undefined'))
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='hackos-net-host-') as directory:
    binary = Path(directory) / 'network'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g',
                    '-fsanitize=' + args.sanitizers, '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'kernel/src'), '-I', str(ROOT / 'common/include'), '-I', str(ROOT / 'include'),
                    str(ROOT / 'tests/host/network.cpp'), str(ROOT / 'tests/host/icmp.cpp'), str(ROOT / 'tests/host/route.cpp'),
                    str(ROOT / 'kernel/src/net/route.cpp'),
                    str(ROOT / 'tests/host/dns.cpp'), str(ROOT / 'kernel/src/net/dns.cpp'),
                    str(ROOT / 'kernel/src/net/udp.cpp'), str(ROOT / 'kernel/src/net/resolve.cpp'),
                    str(ROOT / 'kernel/src/net/icmp.cpp'), str(ROOT / 'kernel/src/net/ping.cpp'), str(ROOT / 'kernel/src/net/arp.cpp'),
                    '-o', str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=30)
print(f'PASS Ethernet/ARP/IPv4/ICMP/UDP/DNS and request lifecycles ({args.sanitizers})')
