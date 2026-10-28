#!/usr/bin/env python3
"""Boot the real libgui consumer and inspect widgets, pixels, ownership and waits."""
import argparse
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD


def test(firmware, timeout):
    directory = OUT / ('qemu-gui-library-' + firmware)
    report = Report('qemu-gui-library-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        with boot(directory, image, firmware, 'widgets', timeout) as g:
            def send_key(code, shift=False, control=False, alt=False):
                keys = (['ctrl'] if control else []) + (['alt'] if alt else []) + (['shift'] if shift else []) + [code]
                events = [{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': key}}}
                          for down, sequence in [(True, keys), (False, keys[::-1])] for key in sequence]
                g.qmp.call('input-send-event', {'events': events})
                time.sleep(0.04)
            g.qmp.key = send_key
            g.qmp.socket.settimeout(timeout)
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.enter('window --widgets --trace', 'GUI_WIDGETS_READY')

            sequence = 0
            def inspect(checks='', alive=True):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                  '\nadd-symbol-file ' + str(built / 'source/libs/window/bin/window') + '\npython\nimport gdb, struct, time\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
for i in range(3): gdb.execute('continue')
''' + ('''
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    w = gdb.parse_and_eval('KernelWindowManager->focused')
    assert int(w), 'No focused widget window'
    p = gdb.parse_and_eval('ready_queue')
    while int(p) and int(p['pid']) != int(w['owner']): p = p['next']
    if int(p['state']) == 6 and int(w['events']['count']) == 0 and int(gdb.parse_and_eval('mouseEvents.count')) == 0:
        break
    gdb.execute('continue')
else: raise AssertionError('Widget app did not block: state=%s events=%s ingress=%s pending=%s delivered=%s' % (p['state'], w['events']['count'], gdb.parse_and_eval('mouseEvents.count'), w['closePending'], w['closeDelivered']))
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
def flag(expression): return read(expression, 1) != b'\\0'
def text(id): return read('widgetView.widgets[' + str(id) + '].text', 256).split(b'\\0')[0].decode()
def field(id, name): return u('widgetView.widgets[' + str(id) + '].' + name)
''' if alive else '') + checks + "\nbp.delete()\nprint('WIDGET_STATE_OK')\nend\ndetach\nquit\n")
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'widget inspection ' + str(sequence))
                assert 'WIDGET_STATE_OK' in result.stdout, result.stdout
                g.qmp.call('cont')

            def key(code, **kwargs):
                send_key(code, **kwargs)
                inspect()
            def type_text(value):
                for ch in value:
                    g.qmp.type(ch)
                    inspect()

            mouse = [0, 0]
            def move(x, y):
                while mouse != [x, y]:
                    dx, dy = max(-100, min(100, x - mouse[0])), max(-100, min(100, y - mouse[1]))
                    g.qmp.call('input-send-event', {'events': [{'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                                                             {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
                    mouse[0] += dx
                    mouse[1] += dy
                    time.sleep(0.08)
                inspect("assert int(gdb.parse_and_eval('MousePositionX')) == " + str(x) +
                        "\nassert int(gdb.parse_and_eval('MousePositionY')) == " + str(y))
            def button(down):
                g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                inspect()
            def click(x, y):
                move(x, y)
                button(True)
                button(False)

            inspect("assert u('widgetView.count') == 11 and u('widgetView.focused') == 3\nassert text(6) == 'Hello GUI'\nassert int(w['sizeX']) == 720")
            key('tab')
            inspect("assert u('widgetView.focused') == 5")  # disabled button skipped
            key('tab', shift=True)
            mark = len(g.output())
            key('ret')
            g.wait('GUI_WIDGET_APPLY', mark)
            key('tab')
            key('ret')
            inspect("assert flag('widgetView.modal')")
            mark = len(g.output())
            key('f4', alt=True)  # close while About is open must not strand the close latch
            g.wait('GUI_WIDGET_CLOSE_BLOCKED', mark)
            inspect("assert flag('widgetView.modal') and not int(w['closePending'])")
            key('esc')
            inspect("assert not flag('widgetView.modal') and u('widgetView.focused') == 5")
            key('tab')
            key('a', control=True)
            type_text('abcdef')
            key('left', shift=True)
            key('left', shift=True)
            key('c', control=True)
            key('home')
            key('v', control=True)
            inspect("assert text(6) == 'efabcdef' and field(6, 'cursor') == 2")
            key('a', control=True)
            key('x', control=True)
            inspect("assert text(6) == ''")
            key('v', control=True)
            key('backspace')
            inspect("assert text(6) == 'efabcde'")
            key('tab')
            key('spc')
            inspect("assert flag('widgetView.widgets[7].checked')")
            key('tab')
            key('end')
            inspect("assert field(8, 'selected') == 19 and field(8, 'scroll') > 0")
            key('pgup')
            inspect("assert field(8, 'selected') < 19")
            g.screen('widgets')
            # Rendered pixels are checked independently of layout metadata.
            inspect("""
area = w['drawArea']
assert int(area['address'][92 * int(area['pitch']) + 14]) == 0xff101b25
assert int(area['address'][0]) == 0xff17212b
assert int(area['address'][250 * int(area['pitch']) + 702]) == 0xff60cfff
""")
            # Modal input is confined: pointer clicks on the underlying checkbox do nothing.
            key('f4', alt=True)
            inspect("assert flag('widgetView.modal') and int(w['closePending'])")
            g.screen('dialog')
            click(230, 315)  # checkbox is behind dialog/overlay
            inspect("assert flag('widgetView.widgets[7].checked') and flag('widgetView.modal')")
            key('esc')
            inspect("assert not flag('widgetView.modal') and not int(w['closePending'])")
            # Click field, drag a range, then lose capture: reset cancels pending activation.
            move(220, 275)
            button(True)
            move(244, 275)
            button(False)
            inspect("assert u('widgetView.focused') == 6 and field(6, 'anchor') != field(6, 'cursor')")
            key('f4', alt=True)
            key('spc')  # Cancel via Space must not insert its following TEXT into the field.
            inspect("assert not flag('widgetView.modal') and text(6) == 'efabcde'")
            # Maximize/restore lays out the same tree using current frame geometry.
            key('f10', alt=True)
            inspect("assert int(w['maximised']) and field(6, 'bounds.width') > 1000")
            g.screen('maximized')
            key('f10', alt=True)
            inspect("assert not int(w['maximised']) and field(6, 'bounds.width') == 696")
            # Inject ingress loss; committed text must survive while pressed state resets.
            inspect("gdb.execute('set mouseEvents.overflow = true')")
            inspect("assert u('widgetView.pressed') == 0xffffffff and text(6) == 'efabcde'")
            key('f4', alt=True)
            key('tab')  # default Cancel -> Close
            mark = len(g.output())
            send_key('ret')
            g.wait('GUI_WIDGETS_CLOSED', mark)
            g.wait('$ ', mark)
            inspect("assert int(gdb.parse_and_eval('user_windows_live')) == 0\nassert int(gdb.parse_and_eval('window_surface_pages')) == 0", alive=False)
            g.qmp.type('shutdown\n')
            g.process.wait(timeout=timeout)
            assert g.process.returncode == 0
        return 'Widget layout/pixels, disabled traversal, partial selection and clipboard, checkbox/list navigation, modal close routing/isolation/cancel/accept, pointer selection, maximize/restore, overflow, blocked idle and full cleanup.'

    report.check('userspace_gui_library', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
