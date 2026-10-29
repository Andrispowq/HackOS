#!/usr/bin/env python3
"""Measure key dispatch, application commit and presentation on disposable guests."""
import argparse
import json
import os
import shutil
import subprocess
import time
from common import OUT
from qemu_accounts import boot, ADMIN_PASSWORD


def measure(firmware, samples):
    directory = OUT / ('qemu-input-latency-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'
    shutil.copy2(built / 'HackOS.img', image)
    results = {}
    for app in ('login', 'terminal'):
        with boot(directory, image, firmware, app, 120, graphical=app == 'login') as g:
            if app == 'login':
                g.wait('LOGIN_GUI_READY')
            else:
                g.setup()
                g.enter('root', 'New password: ')
                g.password(ADMIN_PASSWORD, 'Action: ')
                g.enter('login', 'Login: ')
                g.enter('root', 'Password: ')
                g.enter(ADMIN_PASSWORD, 'TERMINAL_READY')
                g.wait('root@root:~$ ')
            # Let initial painting finish before injecting the first measured key.
            time.sleep(0.2)
            results[app] = []
            for sample in range(samples):
                script = directory / 'measure.gdb'
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '''
python
import gdb, time, json
stamp = {}
def mark(name):
    stamp[name] = [int(gdb.parse_and_eval('tick')), time.monotonic()]
class Dispatch(gdb.Breakpoint):
    def stop(self):
        key = gdb.parse_and_eval('key')
        if int(key['down']) and int(key['text']):
            mark('dispatch')
            self.enabled = False
        return False
class Commit(gdb.Breakpoint):
    def stop(self):
        if 'dispatch' in stamp and int(gdb.parse_and_eval('window')) == int(gdb.parse_and_eval('KernelWindowManager->focused->ID')):
            mark('commit')
            self.enabled = False
        return False
class Finish(gdb.FinishBreakpoint):
    def stop(self):
        mark('present')
        print('LATENCY ' + json.dumps(stamp), flush=True)
        return True
class Present(gdb.Breakpoint):
    def stop(self):
        if 'commit' in stamp:
            Finish(gdb.newest_frame(), internal=True)
            self.enabled = False
        return False
Dispatch('WindowManager::Keyboard', internal=True)
Commit('Display::WindowCommitted', internal=True)
Present('Display::DrawBackbuffer', internal=True)
print('INPUT_READY', flush=True)
end
continue
detach
quit
''')
                log = directory / (app + '-' + str(sample) + '.log')
                with log.open('w') as output:
                    debugger = subprocess.Popen([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q',
                                                 str(built / 'kernel.elf'), '-x', str(script)],
                                                stdout=output, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 20
                        while 'INPUT_READY' not in log.read_text():
                            assert debugger.poll() is None, log.read_text()
                            assert time.monotonic() < deadline, 'Debugger attach deadline'
                            time.sleep(0.01)
                        sent = time.monotonic()
                        g.qmp.call('input-send-event', {'events': [
                            {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': 'a'}}}
                            for down in (True, False)]})
                        assert debugger.wait(timeout=30) == 0, log.read_text()
                    finally:
                        if debugger.poll() is None:
                            debugger.kill()
                            debugger.wait()
                data = json.loads(next(line[8:] for line in log.read_text().splitlines() if line.startswith('LATENCY ')))
                results[app].append({
                    'dispatch_to_commit_ticks': data['commit'][0] - data['dispatch'][0],
                    'commit_to_present_ticks': data['present'][0] - data['commit'][0],
                    'qmp_to_dispatch_seconds': data['dispatch'][1] - sent,
                    'dispatch_to_commit_seconds': data['commit'][1] - data['dispatch'][1],
                    'commit_to_present_seconds': data['present'][1] - data['commit'][1]})
                time.sleep(0.15)
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
