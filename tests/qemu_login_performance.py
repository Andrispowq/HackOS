#!/usr/bin/env python3
"""Measure hashing and Enter-to-shell latency on a disposable account image."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from common import OUT
from qemu_accounts import boot, ADMIN_PASSWORD


def measure(firmware, samples):
    directory = OUT / ('qemu-login-performance-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'
    shutil.copy2(built / 'HackOS.img', image)
    results = []
    with boot(directory, image, firmware, 'timing', 120) as guest:
        guest.fresh = True
        guest.setup()
        guest.enter('admin', 'New password: ')
        guest.password(ADMIN_PASSWORD, 'Action: ')
        for sample in range(samples):
            guest.enter('login', 'Login: ')
            guest.enter('admin', 'Password: ')
            guest.qmp.type(ADMIN_PASSWORD)
            script = directory / 'timing.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(guest.gdb) + '''
python
import gdb, time
class Done(gdb.FinishBreakpoint):
    def stop(self):
        print('HASH_SECONDS=' + str(time.monotonic() - self.started), flush=True)
        return True
class Start(gdb.Breakpoint):
    def stop(self):
        done = Done(gdb.newest_frame(), internal=True)
        done.started = time.monotonic()
        self.enabled = False
        return False
Start('Pbkdf2Sha256', internal=True)
print('TIMING_READY', flush=True)
end
continue
detach
quit
''')
            log = directory / ('hash-' + str(sample) + '.log')
            with log.open('w') as output:
                debugger = subprocess.Popen([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q',
                                             str(built / 'kernel.elf'), '-x', str(script)],
                                            stdout=output, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 20
                    while 'TIMING_READY' not in log.read_text():
                        assert debugger.poll() is None, log.read_text()
                        assert time.monotonic() < deadline, 'Debugger did not attach'
                        time.sleep(0.05)
                    offset = len(guest.output())
                    start = time.monotonic()
                    guest.qmp.key('ret')
                    guest.wait('admin@root:~$ ', offset)
                    elapsed = time.monotonic() - start
                    assert debugger.wait(timeout=20) == 0, log.read_text()
                finally:
                    if debugger.poll() is None:
                        debugger.kill()
                        debugger.wait()
            hashing = float(next(line.split('=', 1)[1] for line in log.read_text().splitlines()
                                 if line.startswith('HASH_SECONDS=')))
            results.append({'enter_to_shell_seconds': elapsed, 'hash_seconds': hashing,
                            'other_seconds_including_debugger': elapsed - hashing})
            guest.logout()
    (directory / 'timings.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--samples', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.samples <= 20:
        parser.error('--samples must be between 1 and 20')
    measure(args.firmware, args.samples)
