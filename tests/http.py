#!/usr/bin/env python3
"""Sanitize the real freestanding HTTP URL/framing decoder."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitizers', choices=('address,undefined', 'undefined'), default='address,undefined')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='hackos-http-') as directory:
    binary = Path(directory) / 'http'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-g', '-O1',
                    '-fsanitize=' + args.sanitizers, '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'include'), '-I', str(ROOT / 'common/include'),
                    str(ROOT / 'tests/host/http.cpp'), '-o', str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=30)
print(f'PASS HTTP URL/framing, segmentation, truncation, chunking, bounds and binary bodies ({args.sanitizers})')
