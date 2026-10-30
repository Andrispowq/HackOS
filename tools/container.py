#!/usr/bin/env python3
"""Compile the selected firmware path and start its browser console."""
import argparse
from pathlib import Path
from build import build
from run_qemu import run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
parser.add_argument('--build-only', action='store_true')
parser.add_argument('--network', action='store_true', help='Enable experimental E1000 Ethernet/ARP')
args = parser.parse_args()
image = build(args.firmware, Path('/output'), preserve_accounts=True)
if not args.build_only: run(args.firmware, image, web=True, network=args.network)
