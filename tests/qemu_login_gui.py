#!/usr/bin/env python3
"""Graphical first boot, masked login, error dialog, session return and power."""
import argparse
import json
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD


def test(firmware, timeout):
    directory = OUT / ('qemu-login-gui-' + firmware)
    report = Report('qemu-login-gui-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        with boot(directory, image, firmware, 'login', timeout, graphical=True) as g:
            g.wait('LOGIN_GUI_READY')
            sequence = 0
            def inspect(checks=''):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                  '\nadd-symbol-file ' + str(built / 'source/libs/login/bin/login') + '\npython\nimport gdb, struct, time, json\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
bp.silent = True
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    gdb.execute('continue', to_string=True)
    p = gdb.parse_and_eval('ready_queue')
    while int(p) and not int(p['authenticationAgent']): p = p['next']
    assert int(p), 'Missing login agent'
    w = gdb.parse_and_eval('KernelWindowManager->focused')
    if int(w) and int(w['owner']) == int(p['pid']) and int(p['state']) == 6 and int(w['events']['count']) == 0 and int(gdb.parse_and_eval('mouseEvents.count')) == 0:
        break
else: raise AssertionError('Login did not drain input and block')
def read(expression, length):
    address = int(gdb.parse_and_eval('(unsigned long)&' + expression))
    result = b''
    while length:
        leaf = p['userSpace']['leaf']['entries'][(address - 0x400000000000) // 4096]
        physical = (int(leaf['address']) << 12) + (address & 4095)
        size = min(length, 4096 - (address & 4095))
        result += bytes(gdb.selected_inferior().read_memory(physical, size))
        address += size
        length -= size
    return result
def u(expression): return struct.unpack('<I', read(expression, 4))[0]
def field(name): return 'loginView.widgets[' + str(u(name)) + ']'
def length(name): return u(field(name) + '.length')
def empty(name): return read(field(name) + '.text', 256) == bytes(256)
def flag(expression): return read(expression, 1) != bytes(1)
''' + checks + '''
positions = {}
for name in ['usernameField', 'passwordField', 'confirmField', 'submitButton', 'shutdownButton', 'restartButton', 'recoveryButton']:
    r = field(name) + '.bounds'
    positions[name] = [int(w['startX']) + u(r + '.x') + u(r + '.width') // 2,
                       int(w['startY']) + 30 + u(r + '.y') + u(r + '.height') // 2]
print('LOGIN_POSITIONS=' + json.dumps(positions))
bp.delete()
end
detach
quit
''')
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'login inspection')
                positions = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('LOGIN_POSITIONS=')))
                g.qmp.call('cont')
                return positions

            def key(code, shift=False, control=False, alt=False):
                keys = (['ctrl'] if control else []) + (['alt'] if alt else []) + (['shift'] if shift else []) + [code]
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': k}}}
                    for down, seq in [(True, keys), (False, keys[::-1])] for k in seq]})
                time.sleep(0.06)
            g.qmp.key = key
            mouse = [0, 0]
            def click(name):
                x, y = inspect()[name]
                while mouse != [x, y]:
                    dx, dy = max(-100, min(100, x-mouse[0])), max(-100, min(100, y-mouse[1]))
                    g.qmp.call('input-send-event', {'events': [
                        {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                        {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
                    mouse[0] += dx
                    mouse[1] += dy
                    time.sleep(0.04)
                inspect()
                for down in (True, False):
                    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                    time.sleep(0.1)

            def fill(name, text):
                click(name)
                key('a', control=True)
                for i in range(0, len(text), 4):
                    g.qmp.type(text[i:i+4])
                    inspect('assert length(' + repr(name) + ') == ' + str(min(i+4, len(text))))

            inspect("assert flag(field('confirmField') + '.visible')\nassert flag(field('passwordField') + '.secret')")
            fill('usernameField', 'root')
            fill('passwordField', ADMIN_PASSWORD)
            fill('confirmField', 'Mismatch123')
            click('submitButton')
            inspect("assert flag('loginView.modal')\nassert empty('passwordField') and empty('confirmField')")
            key('ret')
            inspect("assert not flag('loginView.modal')")
            fill('passwordField', ADMIN_PASSWORD)
            fill('confirmField', ADMIN_PASSWORD)
            g.screen('first-boot-masked')
            mark = len(g.output())
            click('submitButton')
            g.wait('GUI_SETUP_OK', mark)
            g.wait('LOGIN_GUI_READY', mark)
            inspect("assert not flag(field('confirmField') + '.visible')")
            fill('usernameField', 'root')
            fill('passwordField', 'WrongPass123')
            mark = len(g.output())
            click('submitButton')
            g.wait('GUI_LOGIN_FAILED', mark)
            inspect("assert flag('loginView.modal')\nassert empty('passwordField') and empty('confirmField')\nassert int(gdb.parse_and_eval('user_windows_live')) == 1")
            g.screen('login-error')
            key('ret')
            inspect("assert not flag('loginView.modal')")
            fill('passwordField', ADMIN_PASSWORD)
            g.screen('login-masked')
            mark = len(g.output())
            click('submitButton')
            g.wait('GUI_LOGIN_OK', mark)
            g.wait('TERMINAL_READY', mark)
            g.wait('root@root:~$ ', mark)
            # Force desktop exit through its established recovery shortcut.
            key('esc', alt=True)
            time.sleep(0.3)
            mark = len(g.output())
            key('esc', alt=True, control=True)
            g.wait('LOGIN_GUI_READY', mark)
            inspect("assert empty('passwordField') and empty('confirmField')\nassert length('usernameField') == 0\nassert int(gdb.parse_and_eval('user_windows_live')) == 1")
            g.screen('login-return')
            click('shutdownButton')
            g.process.wait(timeout=30)
            assert 'Stopping the CPU.' in g.output()
        # A separate boot of the saved account verifies the restart button.
        with boot(directory, image, firmware, 'restart', timeout, graphical=True, reboot=True) as g:
            g.wait('LOGIN_GUI_READY')
            # Login form traversal: username, password, login, shutdown, restart.
            for _ in range(4):
                g.qmp.key('tab')
                time.sleep(0.2)
            g.qmp.key('ret')
            g.wait('Restarting the CPU.')
            g.wait('LOGIN_GUI_READY', g.output().index('Restarting the CPU.'))
        return 'Graphical setup/mismatch; masked fields; failed-login dialog; desktop launch and return; shutdown and restart.'
    report.check('graphical_login', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
