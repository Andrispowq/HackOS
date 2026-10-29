#!/usr/bin/env python3
"""Exercise Files through real GUI input and inspect copied process state."""
import argparse
import json
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem


def test(firmware, timeout):
    directory = OUT / ('qemu-files-gui-' + firmware)
    report = Report('qemu-files-gui-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        for name, data in [('alpha.txt', b'hello\nworld\n'), ('binary.dat', b'\x00\xff\x01'),
                           ('large.txt', b'x' * 40000)]:
            fixture = directory / name
            fixture.write_bytes(data)
            require_success(command(['mcopy', '-i', filesystem(image), fixture, '::/tmp/' + name]), 'fixture')
        many = directory / 'many'
        many.mkdir(exist_ok=True)
        for i in range(130):
            (many / ('f%03d.txt' % i)).write_text('page fixture')
        require_success(command(['mmd', '-i', filesystem(image), '::/tmp/many']), 'paged fixture folder')
        require_success(command(['mcopy', '-i', filesystem(image), *sorted(many.iterdir()), '::/tmp/many/']), 'paged fixtures')
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'files', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.enter('files /tmp', 'FILES_READY')
            sequence = 0

            def inspect(checks=''):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                  '\nadd-symbol-file ' + str(built / 'source/libs/files/bin/files') + '\npython\nimport gdb, struct, time, json\n' + '''
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
else: raise AssertionError('Files did not drain input and block')
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
def field(name): return 'view.widgets[' + str(u(name)) + ']'
def text(expression): return read(expression, 256).split(b'\\0', 1)[0].decode('ascii')
def flag(expression): return read(expression, 1) != bytes(1)
''' + checks + '''
positions = {}
for i in range(u('view.count')):
    f = 'view.widgets[' + str(i) + ']'
    r = f + '.bounds'
    positions[str(i)] = [int(w['startX']) + u(r + '.x') + u(r + '.width') // 2,
                         int(w['startY']) + 30 + u(r + '.y') + u(r + '.height') // 2]
    positions[text(f + '.text')] = positions[str(i)]
for name in ['address', 'target', 'openButton', 'deleteButton', 'renameButton']:
    positions[name] = positions[str(u(name))]
r = field('listing') + '.bounds'
for i in range(u('count')):
    positions['entry:' + text('entries[' + str(i) + '].name').lower()] = [int(w['startX']) + u(r + '.x') + 30,
        int(w['startY']) + 30 + u(r + '.y') + 8 + i * 20 + 8]
positions['windowClose'] = [int(w['startX']) + u('view.width') - 14, int(w['startY']) + 15]
print('FILES_POSITIONS=' + json.dumps(positions))
bp.delete()
end
detach
quit
''')
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'Files inspection')
                assert 'Traceback' not in result.stdout, result.stdout
                positions = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('FILES_POSITIONS=')))
                g.qmp.call('cont')
                return positions

            def key(code, control=False):
                keys = (['ctrl'] if control else []) + [code]
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': k}}}
                    for down, codes in [(True, keys), (False, list(reversed(keys)))] for k in codes]})
                time.sleep(.1)

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
                    time.sleep(.15)

            def fill(name, value):
                click(name)
                key('a', True)
                for c in value:
                    g.qmp.type(c)
                    time.sleep(.04)
                inspect("assert text(field('" + name + "') + '.text') == " + repr(value))

            inspect("assert text('cwd').lower() == '~/tmp'")
            fill('address', '/tmp/many')
            click('Go')
            inspect("assert u('count') == 128 and flag('more')")
            click('Next')
            inspect("assert u('page') == 1 and u('count') == 4 and not flag('more')")
            click('Previous')
            inspect("assert u('page') == 0 and u('count') == 128")
            click('Up')

            click('entry:alpha.txt')
            inspect("assert 'Created: ' in text(field('datesLabel') + '.text')\nassert 'Modified: ' in text(field('datesLabel') + '.text')\nassert 'Unknown' not in text(field('datesLabel') + '.text')")
            click('openButton')
            inspect("assert text('preview[0]') == 'hello'\nassert text('preview[1]') == 'world'")
            mark = len(g.output())
            click('Edit text')
            g.wait('EDITOR_READY', mark)
            time.sleep(.3)
            g.qmp.call('input-send-event', {'events': [
                {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': keycode}}}
                for down, codes in [(True, ['alt', 'f4']), (False, ['f4', 'alt'])] for keycode in codes]})
            g.wait('exited 0', mark)
            inspect("assert text('cwd').lower() == '~/tmp'")
            click('entry:binary.dat')
            click('openButton')
            inspect("assert text('preview[0]') == '00 FF 01 '")
            click('entry:large.txt')
            click('openButton')
            inspect("assert '(truncated)' in text(field('status') + '.text')")
            fill('target', 'notes')
            click('New folder')
            inspect("assert text(field('status') + '.text') == 'Operation completed.'")
            click('entry:notes')
            inspect("assert not flag(field('deleteButton') + '.enabled')")
            click('openButton')
            inspect("assert text('cwd').lower() == '~/tmp/notes'")
            click('Up')
            click('entry:alpha.txt')
            fill('target', 'notes/renamed.txt')
            click('renameButton')
            inspect("assert text(field('status') + '.text') == 'Operation completed.'")
            fill('address', '/tmp/notes')
            click('Go')
            click('entry:renamed.txt')
            click('deleteButton')
            inspect("assert flag('deleting') and flag('view.modal')")
            key('esc')
            inspect("assert not flag('deleting') and u('count') == 3")
            click('deleteButton')
            key('tab')
            key('ret')
            inspect("assert u('count') == 2 and not flag('view.modal')")
            fill('address', '/missing')
            click('Go')
            inspect("assert flag('view.modal')\nassert text('cwd').lower() == '~/tmp/notes'")
            key('esc')
            g.screen('files')
            click('windowClose')
            g.wait('$ ', len(g.output()) - 100)
            g.enter('runas guest files /tmp', 'FILES_READY')
            fill('target', '/denied')
            click('New folder')
            inspect("assert flag('view.modal')\nassert 'Permission denied' in text(field('status') + '.text')")
            g.screen('permission-denied')
            click('windowClose')
            time.sleep(.5)
            g.shell('echo cleanup-ok', 'cleanup-ok')
            time.sleep(.5)
            g.qmp.call('stop')
            script = directory / 'cleanup.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb\n'
                              'assert int(gdb.parse_and_eval("user_files_live")) == 0\n'
                              'assert int(gdb.parse_and_eval("user_windows_live")) == 0\n'
                              'p = gdb.parse_and_eval("ready_queue")\ncount = 0\n'
                              'while int(p):\n count += 1\n p = p["next"]\n'
                              'assert count == 3, count\nprint("FILES_CLEANUP_OK")\nend\ndetach\nquit\n')
            result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
            (directory / 'cleanup.log').write_text(result.stdout)
            require_success(result, 'file manager cleanup')
            assert 'FILES_CLEANUP_OK' in result.stdout
            g.qmp.call('cont')
        listing = require_success(command(['mdir', '-i', filesystem(image), '::/tmp/notes']), 'saved folder')
        assert 'renamed' not in listing.lower(), listing
        return 'Text/hex/truncated previews; navigation; mkdir; move; cancel/confirm delete; missing paths; guest permission errors; close.'
    report.check('file_manager_gui', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
