#!/usr/bin/env python3
"""Settings: actual input, account edits, permission checks and password persistence."""
import argparse
import json
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe


def test(firmware, timeout):
    directory = OUT / ('qemu-settings-' + firmware)
    report = Report('qemu-settings-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / "source", extra=("auth_probe.cpp", "AUTHPROBE"))
        require_success(command(["mcopy", "-i", filesystem(image), probe, "::/USR/BIN/AUTHPROBE"]), "install auth probe")
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'settings', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('authprobe', 'AUTH_BOUNDARIES_OK')
            g.shell('runas guest authprobe', 'AUTH_BOUNDARIES_OK')
            g.enter('settings', 'SETTINGS_READY')
            sequence = 0

            def inspect(checks=''):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                  '\nadd-symbol-file ' + str(built / 'source/libs/settings/bin/settings') + '\npython\nimport gdb, struct, time, json\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
bp.silent = True
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    gdb.execute('continue', to_string=True)
    w = gdb.parse_and_eval('KernelWindowManager->focused')
    p = gdb.parse_and_eval('ready_queue')
    while int(p) and (not int(w) or int(p['pid']) != int(w['owner'])): p = p['next']
    if int(p) and int(p['state']) == 6 and int(w['events']['count']) == 0 and int(gdb.parse_and_eval('mouseEvents.count')) == 0:
        break
else: raise AssertionError('Settings did not drain input and block')
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
def flag(expression): return read(expression, 1) != bytes(1)
def text(expression): return read(expression, 256).split(b'\\0', 1)[0].decode('ascii')
active = 'view'
def field(name): return active + '.widgets[' + str(u(name)) + ']'
''' + checks + '''
positions = {}
for i in range(u(active + '.count')):
    f = active + '.widgets[' + str(i) + ']'
    r = f + '.bounds'
    positions[str(i)] = [int(w['startX']) + u(r + '.x') + u(r + '.width') // 2,
                         int(w['startY']) + 30 + u(r + '.y') + u(r + '.height') // 2]
    positions[text(f + '.text')] = positions[str(i)]
names = ['userName', 'groupName'] if u('currentTab') == 6 else ['oldPassword', 'password', 'confirmation'] if u('currentTab') == 7 else []
for name in names:
    positions[name] = positions[str(u(name))]
if u('currentTab') == 6:
    for listName, countName, arrayName in [('userList', 'userCount', 'users'), ('groupList', 'groupCount', 'groups')]:
        r = field(listName) + '.bounds'
        for i in range(u(countName)):
            positions[arrayName + ':' + text(arrayName + '[' + str(i) + '].name')] = [int(w['startX']) + u(r + '.x') + 30,
                int(w['startY']) + 30 + u(r + '.y') + 10 + i * 20]
positions['windowClose'] = [int(w['startX']) + u(active + '.width') - 14, int(w['startY']) + 15]
print('SETTINGS_POSITIONS=' + json.dumps(positions))
bp.delete()
end
detach
quit
''')
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'Settings inspection')
                positions = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('SETTINGS_POSITIONS=')))
                g.qmp.call('cont')
                return positions

            def key(code, shift=False, control=False, alt=False):
                keys = (['ctrl'] if control else []) + (['alt'] if alt else []) + (['shift'] if shift else []) + [code]
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': k}}}
                    for down, codes in [(True, keys), (False, list(reversed(keys)))] for k in codes]})
                time.sleep(.08)
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
                    time.sleep(.04)
                inspect()
                for down in (True, False):
                    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                    time.sleep(.1)

            def fill(name, value):
                click(name)
                key('a', control=True)
                g.qmp.type(value)
                inspect("assert text(field('" + name + "') + '.text') == " + repr(value))

            inspect("assert u('currentTab') == 0 and u('rowCount') > 2")
            for title in ['CPU', 'Memory', 'Devices', 'Display', 'Firmware']:
                click(title)
                inspect("assert u('rowCount') > 0")
                if title == 'Memory':
                    inspect("assert len(text('rows[3]').split()[0]) == 16 and text('rows[3]')[16:18] == '  '")
                if title == 'Firmware':
                    inspect("assert text('rows[1]')[8:10] == '  ' and text('rows[1]')[18:20] == '  '")
                if title in ('Memory', 'Devices', 'Firmware'):
                    g.screen('aligned-' + title.lower())
            g.screen('system-information')
            click('Accounts')
            fill('groupName', 'staff')
            click('Create group')
            inspect("assert u('groupCount') == 3")
            click('groups:staff')
            fill('userName', 'alice')
            click('Create user')
            inspect("assert u('userCount') == 3")
            click('users:alice')
            fill('userName', 'bob')
            click('Rename user')
            inspect("assert text('users[2].name') == 'bob' and u('users[2].credentials.uid') == 1001")
            click('groups:staff')
            fill('groupName', 'team')
            click('Rename group')
            inspect("assert text('groups[2].name') == 'team'")
            click('users:bob')
            click('groups:users')
            click('Add membership')
            inspect("assert u('users[2].credentials.groupCount') == 1")
            click('users:bob')
            click('groups:users')
            click('Set primary group')
            inspect("assert u('users[2].credentials.gid') == 1000 and u('users[2].credentials.groupCount') == 0")
            click('users:bob')
            fill('userName', 'root')
            click('Rename user')
            inspect("assert text('users[2].name') == 'bob' and 'failed' in text(field('status') + '.text')")
            click('users:guest')
            click('Password')
            fill('password', 'GuestPass123')
            fill('confirmation', 'GuestPass123')
            click("Reset selected user's password")
            inspect("assert text(field('status') + '.text') == 'Password saved.'")
            inspect("assert text(field('password') + '.text') == ''")
            click('Accounts')
            g.screen('accounts')
            mark = len(g.output())
            click('windowClose')
            g.wait('$ ', mark)
            g.enter('runas guest settings', 'SETTINGS_READY')
            inspect("assert u('identity.uid') == 1000 and 'administrator' in text('rows[0]')")
            click('Accounts')
            inspect("assert not flag(field('renameUser') + '.enabled') and not flag(field('createGroup') + '.enabled')")
            g.screen('accounts-read-only')
            click('Password')
            fill('oldPassword', 'WrongPass123')
            fill('password', 'ChangedPass123')
            fill('confirmation', 'ChangedPass123')
            click('Change my password')
            inspect("assert 'denied' in text(field('status') + '.text')")
            time.sleep(1.1)
            fill('oldPassword', 'GuestPass123')
            fill('password', 'ChangedPass123')
            fill('confirmation', 'ChangedPass123')
            click('Change my password')
            inspect("assert text(field('status') + '.text') == 'Password saved.'")
            click('Terminal')
            click('Reset console appearance')
            inspect("assert 'Saved' in text(field('status') + '.text')")
            mark = len(g.output())
            click('windowClose')
            g.wait('$ ', mark)
            g.logout()
            g.login('guest', 'ChangedPass123')
            g.shell('whoami', 'guest')
            g.logout()
        with boot(directory, image, firmware, 'persisted', timeout) as g:
            g.fresh = False
            g.login('root', ADMIN_PASSWORD)
            g.shell('users', 'bob')
            g.shell('groups', 'team')
            g.logout()
            g.login('guest', 'ChangedPass123')
        return 'Hardware tabs; user/group creation, rename and membership; duplicate rejection; read-only guest; password verification/change and reboot persistence.'
    report.check('graphical_settings', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
