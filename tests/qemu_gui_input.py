#!/usr/bin/env python3
"""GUI keyboard, desktop shortcuts, close negotiation and session clipboard."""
import argparse
import os
import re
import shutil
import sys
import time
from common import ROOT, OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-gui-input-' + firmware)
    report = Report('qemu-gui-input-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('gui_input_probe.cpp', 'GUIPROBE'))
        symbols = require_success(command([os.environ.get('CROSS_COMPILE', 'x86_64-elf-') + 'nm', probe]), 'probe symbols')
        gate = int(re.search(r'^([0-9a-f]+) [A-Za-z] gui_gate$', symbols, re.M)[1], 16)
        require_success(command(['mcopy', '-o', '-i', filesystem(image), probe, '::/USR/BIN/GUIPROBE']), 'install GUI probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'gui-input', timeout) as g:
            def send_key(code, shift=False, control=False, alt=False):
                # Explicit transitions avoid send-key's guest-time release timer
                # overlapping the next chord on a heavily loaded TCG host.
                keys = (['ctrl'] if control else []) + (['alt'] if alt else []) + (['shift'] if shift else []) + [code]
                events = [{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': key}}}
                          for down, sequence in [(True, keys), (False, keys[::-1])] for key in sequence]
                g.qmp.call('input-send-event', {'events': events})
                time.sleep(0.025)
            g.qmp.key = send_key
            g.qmp.socket.settimeout(timeout)
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)

            def inspect(name, checks, settle=False):
                g.qmp.call('stop')
                script = directory / (name + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb, time, struct\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
for i in range(3): gdb.execute('continue')
''' + ('''
for attempt in range(200):
    w = gdb.parse_and_eval('KernelWindowManager->focused')
    p = gdb.parse_and_eval('ready_queue')
    while int(p) and int(p['pid']) != int(w['owner']): p = p['next']
    if int(p['state']) == 6 and int(w['events']['count']) == 0 and int(gdb.parse_and_eval('mouseEvents.count')) == 0:
        break
    gdb.execute('continue')
else:
    raise AssertionError('GUI did not finish processing input and block')
''' if settle else '') + '''
bp.delete()
w = gdb.parse_and_eval('KernelWindowManager->focused')
''' + checks + "\nprint('GUI_STATE_OK')\nend\ndetach\nquit\n")
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / (name + '.log')).write_text(result.stdout)
                require_success(result, name)
                assert 'GUI_STATE_OK' in result.stdout, result.stdout
                g.qmp.call('cont')

            edit_number = 0
            def settle_ui():
                nonlocal edit_number
                edit_number += 1
                inspect('edit-ready-' + str(edit_number), '', settle=True)

            def edit_key(code, **kwargs):
                key(code, **kwargs)
                settle_ui()

            def edit_text(text):
                for char in text:
                    g.qmp.type(char)
                    settle_ui()

            def key(code, **kwargs):
                g.qmp.key(code, **kwargs)
                time.sleep(0.06)

            def raw(code, down):
                g.qmp.call('input-send-event', {'events': [{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}]})
                time.sleep(0.06)

            mouse = [0, 0]
            def move(x, y):
                while mouse != [x, y]:
                    dx, dy = max(-100, min(100, x - mouse[0])), max(-100, min(100, y - mouse[1]))
                    g.qmp.call('input-send-event', {'events': [{'type': 'rel', 'data': {'axis': 'x', 'value': dx}}, {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
                    mouse[0] += dx
                    mouse[1] += dy
                    time.sleep(0.06)

            def button(down):
                g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                time.sleep(0.08)

            g.shell('guiprobe &', 'GUI_READY')
            mark = len(g.output())
            raw('shift', True)
            key('a')
            raw('shift', False)
            g.wait('GUI_TEXT 65', mark)
            g.wait('GUI_UP 42 0', mark)
            raw('b', True)
            raw('b', True)
            raw('b', False)
            g.wait('GUI_DOWN 48 0 1', mark)
            key('c', control=True)
            g.wait('GUI_DOWN 46 2 0', mark)
            assert 'GUI_TEXT 3\n' not in g.output()[mark:]
            inspect('released', '''
assert int(w['richInput']) and all(int(w['pressedKeys'][i]) == 0 for i in range(4))
assert int(w['modifiers']) == 0
''')
            key('esc', alt=True)
            g.shell('window --trace &', 'Window demo ready.')
            key('tab', alt=True)
            inspect('switch-forward', "assert w['title'].string() == 'GUI input probe'")
            key('tab', alt=True, shift=True)
            inspect('switch-reverse', "assert w['title'].string() == 'Window input demo'")
            key('tab', alt=True)
            raw('a', True)
            move(120, 150)
            button(True)
            key('tab', alt=True)
            move(230, 250)
            raw('a', False)
            button(False)
            inspect('focus-reset-capture', '''
assert int(gdb.parse_and_eval('KernelWindowManager->capture')) == 0
for i in range(16):
    slot = gdb.parse_and_eval('KernelWindowManager->slots')[i]
    if int(slot['ID']):
        assert int(slot['mouseButtons']) == 0
        assert all(int(slot['pressedKeys'][i]) == 0 for i in range(4))
''')
            key('tab', alt=True)
            key('f9', alt=True)
            inspect('keyboard-minimize', "assert w['title'].string() == 'Window input demo'")
            key('tab', alt=True)
            inspect('keyboard-restore', "assert w['title'].string() == 'GUI input probe' and not int(w['minimised'])")
            key('f10', alt=True)
            inspect('keyboard-maximize', "assert int(w['maximised']) == 1")
            key('f10', alt=True)
            inspect('keyboard-normal', "assert int(w['maximised']) == 0 and int(w['sizeX']) == 240")
            raw('a', True)
            inspect('inject-ingress-loss', "gdb.execute('set mouseEvents.overflow = true')")
            raw('a', False)
            inspect('ingress-recovered', "assert all(int(w['pressedKeys'][i]) == 0 for i in range(4)) and not int(w['mouseButtons'])")
            inspect('inject-lost-interrupt', """
gdb.execute('set mouseEvents.overflow = true')
gdb.execute('set interruptPending = true')
for field, value in [('key', 46), ('decoded', 3), ('text', 0), ('modifiers', 2), ('flags', 0), ('down', 1)]:
    gdb.execute("set 'mouse.cpp'::pendingInterrupt." + field + ' = ' + str(value))
""")
            inspect('no-synthetic-copy', "assert all(int(w['pressedKeys'][i]) == 0 for i in range(4)) and not int(w['modifiers'])")
            mark = len(g.output())
            key('f4', alt=True)
            g.wait('GUI_CLOSE_REJECTED', mark)
            inspect('close-rejected', "assert not int(w['closePending']) and int(w['ID'])")
            key('f4', alt=True)
            g.wait('GUI_DONE', mark)
            key('esc', control=True, alt=True)
            key('esc', alt=True)
            inspect('closed', "assert int(gdb.parse_and_eval('user_windows_live')) == 0")

            g.enter('guiprobe overflow', 'GUI_OVERFLOW_READY')
            g.qmp.type('x' * 100)
            key('f4', alt=True)
            inspect('overflow-close', '''
assert int(w['events']['overflow']) and int(w['closePending'])
p = gdb.parse_and_eval('ready_queue')
while int(p) and int(p['pid']) != int(w['owner']): p = p['next']
va = ''' + str(gate) + '''
entry = p['userSpace']['leaf']['entries'][(va - 0x400000000000) // 4096]
physical = (int(entry['address']) << 12) + (va & 4095)
gdb.selected_inferior().write_memory(physical, struct.pack('<Q', 1))
''')
            g.wait('GUI_OVERFLOW_OK')
            g.wait('$ ', g.output().index('GUI_OVERFLOW_OK'))

            g.enter('guiprobe order', 'GUI_ORDER_READY')
            key('a')
            key('f4', alt=True)
            inspect('close-after-text', """
p = gdb.parse_and_eval('ready_queue')
while int(p) and int(p['pid']) != int(w['owner']): p = p['next']
va = """ + str(gate) + """
entry = p['userSpace']['leaf']['entries'][(va - 0x400000000000) // 4096]
physical = (int(entry['address']) << 12) + (va & 4095)
gdb.selected_inferior().write_memory(physical, struct.pack('<Q', 1))
""")
            g.wait('GUI_ORDER_OK')
            g.wait('$ ', g.output().index('GUI_ORDER_OK'))

            g.enter('window --interaction --trace', 'Window demo ready.')
            settle_ui()
            edit_text('alpha')
            edit_key('a', control=True)
            edit_key('c', control=True)
            edit_key('tab')
            edit_key('v', control=True)
            edit_key('tab')
            edit_text('beta')
            edit_key('tab', shift=True)
            edit_key('a', control=True)
            edit_key('x', control=True)
            edit_key('tab', shift=True)
            edit_key('a', control=True)
            edit_key('v', control=True)
            mark = len(g.output())
            key('s', control=True)
            g.wait('GUI_SAVED', mark)
            g.wait('WINDOW_PAINT 640 280', g.output().index('GUI_SAVED', mark))
            saved = require_success(command(['mtype', '-i', filesystem(image), '::/tmp/gui-notes.txt']), 'saved GUI fields')
            assert saved == 'alpha\n\nbeta\n', repr(saved)
            g.screen('fields')
            # Verify the packaged PSF font was loaded and drawn in the title label.
            raw_image = (directory / 'fields.ppm').read_bytes().split(b'\n', 3)[3]
            white = sum(raw_image[(y * 1920 + x) * 3:(y * 1920 + x) * 3 + 3] == b'\xee\xee\xee'
                        for y in range(242, 258) for x in range(216, 392))
            assert white > 100, 'Missing userspace font glyphs'
            key('1', shift=True)
            mark = len(g.output())
            key('f4', alt=True)
            g.wait('GUI_CONFIRM', mark)
            g.wait('WINDOW_PAINT 640 280', g.output().index('GUI_CONFIRM', mark))
            g.screen('unsaved-dialog')
            key('esc')
            g.wait('GUI_CANCEL', mark)
            mark = len(g.output())
            key('f4', alt=True)
            g.wait('GUI_CONFIRM', mark)
            key('tab', shift=True)  # Cancel -> Discard
            key('ret')
            g.wait('Window demo closed.', mark)
            g.wait('$ ', mark)

            g.enter('window --interaction --trace', 'Window demo ready.')
            settle_ui()
            move(230, 425)  # third field, using pointer focus
            button(True)
            button(False)
            settle_ui()
            edit_text('az')
            edit_key('left')
            edit_key('backspace')
            edit_text('beta')
            edit_key('delete')
            mark = len(g.output())
            key('f4', alt=True)
            g.wait('GUI_CONFIRM', mark)
            key('tab')  # Cancel -> Save and close
            key('ret')
            g.wait('GUI_SAVED', mark)
            g.wait('Window demo closed.', mark)
            g.wait('$ ', mark)
            saved = require_success(command(['mtype', '-i', filesystem(image), '::/tmp/gui-notes.txt']), 'save and close')
            assert saved == '\n\nbeta\n', repr(saved)

            g.shell('guiprobe lock &', 'GUI_LOCKED')
            g.enter('window --interaction --trace', 'Window demo ready.')
            settle_ui()
            edit_text('keep me')
            mark = len(g.output())
            key('s', control=True)
            g.wait('GUI_SAVE_FAILED', mark)
            key('f4', alt=True)
            g.wait('GUI_CONFIRM', mark)
            key('tab')  # Cancel -> Save and close
            mark = len(g.output())
            key('ret')
            g.wait('GUI_SAVE_FAILED', mark)
            inspect('failed-save-keeps-window', 'assert int(w) and int(w["closePending"])')
            key('esc')
            key('esc', control=True, alt=True)
            g.wait('$ ', mark)
            g.logout()  # also terminates the exclusive writer
            inspect('clipboard-session-cleared', '''
assert int(gdb.parse_and_eval('clipboardSession')) == 0
assert int(gdb.parse_and_eval('clipboardLength')) == 0
assert all(int(gdb.parse_and_eval('clipboardData')[i]) == 0 for i in range(4096))
assert int(gdb.parse_and_eval('user_windows_live')) == 0
assert int(gdb.parse_and_eval('window_surface_pages')) == 0
''')
            g.login('root', ADMIN_PASSWORD)
            g.shell('guiprobe empty', 'GUI_EMPTY_OK')
            g.qmp.type('shutdown\n')
            g.process.wait(timeout=timeout)
            assert g.process.returncode == 0
        return 'Key transitions/repeat/modifiers/text, Ctrl-C safety, window cycling/modes, focus/capture reset, overflow, close rejection/acceptance, clipboard authorization/bounds/session clearing, field navigation, copy/cut/paste, persisted save and failed-save preservation.'

    report.check('gui_input_and_interaction', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
