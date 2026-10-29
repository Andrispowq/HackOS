#!/usr/bin/env python3
"""Exercise authenticated desktop, isolated terminals, job control and recovery."""
import argparse
import os
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD
from qemu_shell import build_probe
from account_image import filesystem


def test(firmware, timeout):
    directory = OUT / ('qemu-desktop-' + firmware)
    report = Report('qemu-desktop-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('desktop_probe.cpp', 'DESKPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/DESKPROBE']), 'install desktop probe')
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'desktop', timeout) as g:
            g.qmp.socket.settimeout(timeout)
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.enter('login', 'Login: ')
            g.enter('root', 'Password: ')
            g.enter(ADMIN_PASSWORD, 'DESKTOP_READY')
            g.wait('root@root:~$ ')
            sequence = 0

            def inspect(checks):
                nonlocal sequence
                sequence += 1
                g.qmp.call('stop')
                script = directory / ('inspect-' + str(sequence) + '.gdb')
                script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\nadd-symbol-file ' + str(built / 'source/libs/desktop/bin/desktop') + '\npython\nimport gdb, time, struct\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
bp.silent = True
deadline = time.monotonic() + 60
last = ''
while time.monotonic() < deadline:
    gdb.execute('continue', to_string=True)
    try:
        def number(expr): return int(gdb.parse_and_eval(expr))
        def grid(index):
            tty = gdb.parse_and_eval('terminals[' + str(index) + ']')
            return bytes(gdb.selected_inferior().read_memory(tty['cells'].address, 9600)).decode('ascii', errors='replace')
        def processes():
            p = gdb.parse_and_eval('ready_queue')
            result = []
            while int(p):
                result.append(p.dereference())
                p = p['next']
            return result
''' + '\n'.join('        ' + line for line in checks.splitlines()) + '''
        break
    except AssertionError as error:
        last = str(error)
else: raise AssertionError('Desktop state deadline: ' + last)
bp.delete()
print('DESKTOP_STATE_OK')
end
detach
quit
''')
                result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                (directory / ('inspect-' + str(sequence) + '.log')).write_text(result.stdout)
                require_success(result, 'desktop inspection')
                g.qmp.call('cont')

            def key(code, shift=False, control=False, alt=False):
                keys = (['alt'] if alt else []) + (['ctrl'] if control else []) + (['shift'] if shift else []) + [code]
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': item}}}
                    for down, seq in [(True, keys), (False, keys[::-1])] for item in seq]})
                time.sleep(0.1)

            # Explicit releases avoid QEMU send-key timers overlapping during
            # framebuffer-heavy TCG scheduling. Use the same path for typing.
            g.qmp.key = key

            def type_command(text):
                for ch in text:
                    mark = len(g.output())
                    g.qmp.type(ch)
                    g.wait(ch, mark)
                g.qmp.type('\n')

            def terminal_enter(text, expected):
                mark = len(g.output())
                type_command(text)
                g.wait(expected, mark)

            def shell(text, expected):
                mark = len(g.output())
                type_command(text)
                g.wait(expected, mark)
                g.wait('$ ', mark)

            mouse = [0, 0]
            def click(x, y):
                while mouse != [x, y]:
                    dx, dy = max(-100, min(100, x - mouse[0])), max(-100, min(100, y - mouse[1]))
                    g.qmp.call('input-send-event', {'events': [
                        {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                        {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
                    mouse[0] += dx
                    mouse[1] += dy
                    time.sleep(0.04)
                inspect("assert number('MousePositionX') == " + str(x) + "\nassert number('MousePositionY') == " + str(y))
                for down in (True, False):
                    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                    time.sleep(0.1)

            inspect("assert number('terminals[0].controller') != 0\nassert number('KernelWindowManager->panel') != 0")
            inspect("""desktop = next(p for p in processes() if int(p['desktopController']))
def readDesktop(expr, size):
    address = number('(unsigned long)&' + expr)
    data = b''
    while size:
        leaf = desktop['userSpace']['leaf']['entries'][(address - 0x400000000000) // 4096]
        amount = min(size, 4096 - (address & 4095))
        data += bytes(gdb.selected_inferior().read_memory((int(leaf['address']) << 12) + (address & 4095), amount))
        address += amount
        size -= amount
    return data
count = struct.unpack('<I', readDesktop('view.count', 4))[0]
labels = [readDesktop('view.widgets[' + str(i) + '].text', 256).split(bytes(1), 1)[0].decode('ascii') for i in range(count)]
clocks = [label for label in labels if label.endswith(' UTC')]
assert len(clocks) == 1 and len(clocks[0]) == 12 and clocks[0][:2].isdigit(), clocks
assert 'Settings' in labels""")
            shell('echo FIRST_TERMINAL', 'FIRST_TERMINAL')
            shell('cd /tmp', '$ ')
            key('esc', alt=True)
            key('ret')
            inspect("assert number('terminals[1].controller') != 0\nassert 'root@root:~$ ' in grid(1)")
            shell('echo SECOND_TERMINAL', 'SECOND_TERMINAL')
            shell('deskprobe', 'DESKTOP_DENIALS_OK')
            shell('echo pipeline | tr a-z A-Z > /tmp/desktop-pipe', '$ ')
            shell('cat /tmp/desktop-pipe', 'PIPELINE')
            inspect("assert 'FIRST_TERMINAL' in grid(0)\nassert 'root@root:~/TMP$ ' in grid(0)\nassert 'root@root:~$ ' in grid(1)\nassert 'SECOND_TERMINAL' not in grid(0)\nassert 'SECOND_TERMINAL' in grid(1)\nassert 'FIRST_TERMINAL' not in grid(1)\nassert number('terminals[0].owner') != number('terminals[1].owner')")
            inspect("assert grid(1).splitlines()[0].find('HackOS') < 100")
            g.screen('two-terminals')
            # Taskbar focus, selected-window minimize and restore.
            click(50, 1040)
            inspect("assert number('KernelWindowManager->focused->owner') == number('terminals[0].controller')")
            click(260, 1000)
            inspect("assert number('KernelWindowManager->slots[2].minimised') == 1")
            click(50, 1040)
            inspect("assert number('KernelWindowManager->focused->owner') == number('terminals[0].controller')\nassert number('KernelWindowManager->slots[2].minimised') == 0")
            # Maximize respects the taskbar and preserves the row-aligned grid.
            key('f10', alt=True)
            inspect("assert number('terminals[0].columns') == 160\nassert number('KernelWindowManager->focused->sizeY') < 950")
            key('f10', alt=True)
            inspect("assert number('terminals[0].columns') == 90")
            click(1200, 1040)
            inspect("assert number('KernelWindowManager->focused->owner') == number('terminals[1].controller')")
            terminal_enter('input', 'input> ')
            mark = len(g.output())
            key('z', control=True)
            g.wait('$ ', mark)
            shell('jobs', 'Stopped')
            type_command('fg')
            inspect("assert number('terminals[1].owner') != number('terminals[1].shell')")
            mark = len(g.output())
            key('c', control=True)
            g.wait('$ ', mark)
            terminal_enter('exit', 'exited')
            inspect("assert number('terminals[1].controller') == 0\nassert number('terminals[0].controller') != 0")
            # Closing the last terminal leaves the launcher usable.
            key('tab', alt=True)
            key('f4', alt=True)
            inspect("assert number('terminals[0].controller') == 0\nassert number('KernelWindowManager->panel') != 0")
            key('esc', alt=True)
            key('ret')
            inspect("assert number('terminals[0].controller') != 0\nassert 'root@root:~$ ' in grid(0)")
            g.screen('desktop-recovered')
            click(50, 1040)
            click(420, 1000)
            inspect("assert number('terminals[0].controller') == 0\nassert number('KernelWindowManager->panel') != 0")
            # Background icons stay behind windows and launch the same applications.
            click(60, 80)
            inspect("assert number('terminals[0].controller') != 0")
            key('f4', alt=True)
            inspect("assert number('terminals[0].controller') == 0")
            mark = len(g.output())
            click(60, 180)
            g.wait('FILES_READY', mark)
            inspect("assert number('user_windows_live') == 3")
            key('f4', alt=True)
            inspect("assert number('user_windows_live') == 2")
            # Launch Files from the authenticated launcher and close it cleanly.
            mark = len(g.output())
            click(600, 1000)
            g.wait('FILES_READY', mark)
            inspect("assert number('user_windows_live') == 3")
            g.screen('launcher-files')
            key('f4', alt=True)
            inspect("assert number('user_windows_live') == 2")
            mark = len(g.output())
            click(750, 1000)
            g.wait('SETTINGS_READY', mark)
            inspect("assert number('user_windows_live') == 3")
            g.screen('launcher-settings-clock')
            key('f4', alt=True)
            inspect("assert number('user_windows_live') == 2")
            # Launch a GUI application from the same authenticated launcher.
            mark = len(g.output())
            click(160, 1000)
            g.wait('GUI_WIDGETS_READY', mark)
            inspect("assert number('user_windows_live') == 3")
            g.screen('launcher-widgets')
            # Reopen a terminal while the GUI application remains alive;
            # logout must reclaim both applications and all their resources.
            click(50, 1000)
            inspect("assert number('terminals[0].controller') != 0\nassert 'root@root:~$ ' in grid(0)")
            mark = len(g.output())
            type_command('logout')
            g.wait('Logged out.', mark)
            g.wait('Action: ', mark)
            inspect("assert number('KernelWindowManager->panel') == 0\nassert all(number('terminals[' + str(i) + '].controller') == 0 for i in range(8))\nassert not any(int(p['sessionId']) for p in processes() if int(p['state']) != 1)\nassert number('user_windows_live') == 0")
            g.login('root', ADMIN_PASSWORD)
            shell('echo RECOVERY_CONSOLE', 'RECOVERY_CONSOLE')
            shell('deskprobe --orphan', 'ORPHAN_SIGNALS_OK')
            # The child shell also printed a prompt; wait for parent input ownership
            # and orphan reclamation before typing into the recovery shell.
            inspect("assert len(processes()) == 3\nassert any(int(p['state']) == 4 and not int(p['authenticationAgent']) for p in processes())")
            g.logout()
            g.enter('login', 'Login: ')
            g.enter('root', 'Password: ')
            g.enter(ADMIN_PASSWORD, 'DESKTOP_READY')
            inspect("assert number('KernelWindowManager->panel') != 0\nassert number('terminals[0].controller') != 0")
            # Exercise endpoint capacity and rollback of a ninth controller.
            for count in range(2, 9):
                key('esc', alt=True)
                key('ret')
                inspect("assert sum(number('terminals[' + str(i) + '].controller') != 0 for i in range(8)) == " + str(count))
            key('esc', alt=True)
            mark = len(g.output())
            key('ret')
            g.wait('exited 1', mark)
            inspect("assert sum(number('terminals[' + str(i) + '].controller') != 0 for i in range(8)) == 8\nassert number('user_windows_live') == 10")
            g.screen('terminal-capacity')
            # A failed or forcibly stopped desktop must return to login, not
            # strand the terminal controller or leave privileged descendants.
            key('esc', alt=True)
            mark = len(g.output())
            key('esc', alt=True, control=True)
            g.wait('Logged out.', mark)
            inspect("assert number('KernelWindowManager->panel') == 0\nassert number('user_windows_live') == 0\nassert all(number('terminals[' + str(i) + '].controller') == 0 for i in range(8))")
            return 'Desktop login; launcher and taskbar focus/minimize/restore/close; isolated grids/cwd/input; terminal jobs; resize; ownership and malformed ABI denial; eight-endpoint limit; logout, console and forced-desktop recovery.'
    report.check('desktop_and_terminals', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
