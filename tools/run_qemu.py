#!/usr/bin/env python3
"""Run the same TCG machine on every host, optionally through a browser console."""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def run(firmware, image, web=False, debug=False, snapshot=False):
    if not image.is_file(): raise FileNotFoundError(f'Build the {firmware} image first: {image}')
    with tempfile.TemporaryDirectory(prefix='hackos-run-') as temporary:
        args = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg', '-cpu', 'qemu64,+rdrand',
                '-m', '1G', '-smp', 'cores=4,threads=1,sockets=1', '-net', 'none',
                '-drive', f'file={image.resolve()},format=raw',
                '-usb', '-device', 'usb-ehci,id=ehci', '-device', 'qemu-xhci,id=xhci',
                '-monitor', 'none', '-serial', 'none', '-no-reboot']
        if snapshot: args += ['-snapshot']
        if firmware == 'uefi':
            ovmf = ROOT / 'boot/UEFI/OVMFbin'
            variables = Path(temporary) / 'OVMF_VARS.fd'
            shutil.copy2(ovmf / 'OVMF_VARS-pure-efi.fd', variables)
            args += ['-drive', f'if=pflash,format=raw,unit=0,file={ovmf}/OVMF_CODE-pure-efi.fd,readonly=on',
                     '-drive', f'if=pflash,format=raw,unit=1,file={variables}']
        if debug: args += ['-S', '-gdb', 'tcp:127.0.0.1:1234']
        if web: args += ['-display', 'none', '-vnc', '127.0.0.1:0']
        processes = []
        def stop(_signal, _frame): raise KeyboardInterrupt
        old_handler = signal.signal(signal.SIGTERM, stop)
        try:
            env = os.environ.copy()
            env['TMPDIR'] = temporary
            qemu = subprocess.Popen(args, env=env)
            processes.append(qemu)
            if web:
                proxy = subprocess.Popen(['websockify', '--web=/usr/share/novnc', '0.0.0.0:6080', '127.0.0.1:5900'])
                processes.append(proxy)
                print('Console: http://localhost:6080/vnc.html?autoconnect=true&resize=scale', flush=True)
            while qemu.poll() is None:
                if web and proxy.poll() is not None: raise RuntimeError('Browser console exited unexpectedly')
                time.sleep(0.2)
            if qemu.returncode: raise RuntimeError(f'QEMU exited with status {qemu.returncode}')
        except KeyboardInterrupt:
            pass
        finally:
            for process in reversed(processes):
                if process.poll() is None: process.terminate()
            for process in processes:
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
            signal.signal(signal.SIGTERM, old_handler)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--web', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--snapshot', action='store_true', help='Discard guest writes when QEMU stops')
    args = parser.parse_args()
    run(args.firmware, args.image, args.web, args.debug, args.snapshot)
