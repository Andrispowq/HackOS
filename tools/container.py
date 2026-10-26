#!/usr/bin/env python3
"""Compile the selected firmware path and start its browser console."""
import argparse
from pathlib import Path
from build import build
from run_qemu import run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
parser.add_argument('--build-only', action='store_true')
args = parser.parse_args()
image = build(args.firmware, Path('/output'))
if not args.build_only: run(args.firmware, image, web=True)
