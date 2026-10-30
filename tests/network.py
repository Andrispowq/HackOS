#!/usr/bin/env python3
"""Sanitize the real Ethernet/ARP codec with malformed and truncated frames."""
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
                    '-I', str(ROOT / 'kernel/src'), '-I', str(ROOT / 'include'),
                    str(ROOT / 'tests/host/network.cpp'), str(ROOT / 'kernel/src/net/arp.cpp'),
                    '-o', str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=30)
print(f'PASS Ethernet/ARP codec ({args.sanitizers})')
