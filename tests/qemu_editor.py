#!/usr/bin/env python3
"""Graphical editor: actual input, file dialogs, clipboard, safety and cleanup."""
import argparse
import json
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem


def test(firmware, timeout):
    directory = OUT / ('qemu-editor-' + firmware)
    report = Report('qemu-editor-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        for name, data in [('bad.dat', b'\x00\xff'), ('huge.txt', b'a' * 32769), ('existing.txt', b'original\n')]:
            fixture = directory / name
            fixture.write_bytes(data)
            require_success(command(['mcopy', '-i', filesystem(image), fixture, '::/tmp/' + name]), 'fixture')
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'editor', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.enter('editor', 'EDITOR_READY')
            sequence = 0

            def inspect(checks=''):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                  '\nadd-symbol-file ' + str(built / 'source/libs/editor/bin/editor') + '\npython\nimport gdb, struct, time, json\n' + '''
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
else: raise AssertionError('Editor did not drain input and block')
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
def document(): return read('document.text', u('document.length')).decode('ascii')
active = 'picker' if flag('picking') else 'view'
def field(name): return active + '.widgets[' + str(u(name)) + ']'
''' + checks + '''
positions = {}
for i in range(u(active + '.count')):
    f = active + '.widgets[' + str(i) + ']'
    r = f + '.bounds'
    positions[str(i)] = [int(w['startX']) + u(r + '.x') + u(r + '.width') // 2,
                         int(w['startY']) + 30 + u(r + '.y') + u(r + '.height') // 2]
    positions[text(f + '.text')] = positions[str(i)]
for name in (['pickerPath', 'pickerDirectory', 'pickerAccept'] if flag('picking') else ['area']):
    positions[name] = positions[str(u(name))]
if flag('picking'):
    r = field('pickerList') + '.bounds'
    for i in range(u('entryCount')):
        positions['entry:' + text('entries[' + str(i) + '].name').lower()] = [int(w['startX']) + u(r + '.x') + 30,
            int(w['startY']) + 30 + u(r + '.y') + 10 + i * 20]
positions['windowClose'] = [int(w['startX']) + u(active + '.width') - 14, int(w['startY']) + 15]
print('EDITOR_POSITIONS=' + json.dumps(positions))
bp.delete()
end
detach
quit
''')
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'Editor inspection')
                positions = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('EDITOR_POSITIONS=')))
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

            inspect("assert document() == '' and not flag('document.modified')")
            g.qmp.type('alpha\nbeta')
            inspect("assert document() == 'alpha\\nbeta' and flag('document.modified')")
            key('a', control=True)
            key('c', control=True)
            key('end', control=True)
            key('ret')
            key('v', control=True)
            inspect("assert document() == 'alpha\\nbeta\\nalpha\\nbeta'")
            key('home', control=True)
            key('right', shift=True)
            g.qmp.type('A')
            inspect("assert document().startswith('Alpha')")
            key('s', control=True)
            inspect("assert flag('picking') and flag('saving')")
            click('entry:tmp')
            click('Browse selection')
            inspect("assert text('directory') == '~/TMP'")
            fill('pickerPath', 'note.txt')
            click('pickerAccept')
            inspect("assert not flag('picking') and not flag('document.modified')\nassert text('currentPath') == '~/TMP/NOTE.TXT'")
            key('end', control=True)
            g.qmp.type('z')
            key('n', control=True)
            inspect("assert flag('view.modal')")
            key('esc')
            inspect("assert document().endswith('z') and flag('document.modified')")
            # Save-as must not overwrite without explicit confirmation.
            click('Save as...')
            fill('pickerPath', '/tmp/existing.txt')
            click('pickerAccept')
            inspect("assert flag('overwrite') and flag('picker.modal')")
            key('esc')
            click('pickerAccept')
            key('tab')
            key('ret')
            inspect("assert not flag('picking') and not flag('document.modified')")
            key('f10', alt=True)
            inspect("assert document() == 'Alpha\\nbeta\\nalpha\\nbetaz'")
            key('f10', alt=True)
            inspect("assert not flag('document.modified')")
            g.screen('multiline-editor')
            # Invalid input preserves the current document.
            for filename in ['/tmp/bad.dat', '/tmp/huge.txt']:
                key('o', control=True)
                fill('pickerPath', filename)
                click('pickerAccept')
                inspect("assert flag('picker.modal')\nassert document() == 'Alpha\\nbeta\\nalpha\\nbetaz'")
                key('esc')
                click('Cancel')
            key('n', control=True)
            inspect("assert document() == ''")
            g.qmp.type('saved on close')
            click('windowClose')
            key('tab')
            key('ret')
            inspect("assert flag('picking') and flag('pendingClose')")
            fill('pickerPath', '/tmp/close.txt')
            mark = len(g.output())
            click('pickerAccept')
            g.wait('$ ', mark)
            g.enter('editor', 'EDITOR_READY')
            inspect("assert document() == ''")
            key('o', control=True)
            fill('pickerPath', '/tmp/note.txt')
            click('pickerAccept')
            inspect("assert document() == 'Alpha\\nbeta\\nalpha\\nbeta'")
            # Close-cancel releases the negotiated-close latch, then discard closes.
            g.qmp.type('unsaved')
            click('windowClose')
            inspect("assert flag('pendingClose') and flag('view.modal')")
            key('esc')
            inspect("assert not flag('pendingClose') and flag('document.modified')")
            click('windowClose')
            key('tab')
            key('tab')
            mark = len(g.output())
            key('ret')
            g.wait('$ ', mark)
            g.enter('runas guest editor /usr/hello.txt', 'EDITOR_READY')
            inspect("assert not flag('document.modified')")
            key('end', control=True)
            g.qmp.type('kept')
            key('s', control=True)
            inspect("assert flag('view.modal') and flag('document.modified')\nassert document().endswith('kept')")
            g.screen('save-denied')
            key('esc')
            click('windowClose')
            key('tab')
            key('tab')
            mark = len(g.output())
            key('ret')
            g.wait('$ ', mark)
            time.sleep(.5)
            g.qmp.call('stop')
            script = directory / 'cleanup.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb\n'
                              'assert int(gdb.parse_and_eval("user_files_live")) == 0\n'
                              'assert int(gdb.parse_and_eval("user_windows_live")) == 0\n'
                              'p = gdb.parse_and_eval("ready_queue")\ncount = 0\n'
                              'while int(p):\n count += 1\n p = p["next"]\n'
                              'assert count == 3, count\nprint("EDITOR_CLEANUP_OK")\nend\ndetach\nquit\n')
            result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
            (directory / 'cleanup.log').write_text(result.stdout)
            require_success(result, 'editor cleanup')
            assert 'EDITOR_CLEANUP_OK' in result.stdout
            g.qmp.call('cont')
        for path, expected in [('/TMP/NOTE.TXT', 'Alpha\nbeta\nalpha\nbeta'),
                               ('/TMP/EXISTING.TXT', 'Alpha\nbeta\nalpha\nbetaz'),
                               ('/TMP/CLOSE.TXT', 'saved on close')]:
            actual = require_success(command(['mtype', '-i', filesystem(image), '::' + path]), 'saved text')
            assert actual == expected, repr(actual)
        return 'Multiline input/selection/clipboard; open/save/save-as; overwrite confirmation; invalid-file preservation; unsaved close; denied save; cleanup.'
    report.check('graphical_editor', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
