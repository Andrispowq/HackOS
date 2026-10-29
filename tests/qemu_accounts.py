#!/usr/bin/env python3
"""Real account setup, persistence, logout and failure recovery on disposable disks."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from common import ROOT, OUT, Report, command, require_success
from qemu_shell import QMP, build_probe
sys.path.insert(0, str(ROOT / 'tools'))
from account_image import filesystem, read_accounts, write_accounts

ADMIN_PASSWORD = 'AdminPass123'
ALICE_PASSWORD = 'AlicePass123'
CHANGED_PASSWORD = 'ChangedPass123'


class Guest:
    def output(self):
        return self.console.read_text(errors='replace')

    def wait(self, text, offset=0):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            output = self.output()
            assert 'Exception:' not in output and 'PROBE_FAIL' not in output, output[-2000:]
            assert self.process.poll() is None, 'QEMU exited: ' + self.output()[-1000:]
            if text in output[offset:]: return
            time.sleep(0.05)
        raise AssertionError('Timed out waiting for ' + repr(text) + ': ' + self.output()[-2000:])

    def enter(self, text, expected):
        offset = len(self.output())
        self.qmp.type(text + '\n')
        self.wait(expected, offset)
        return self.output()[offset:]

    def password(self, password, expected):
        self.enter(password, 'Confirm password: ')
        offset = len(self.output())
        self.enter(password, expected)
        if expected == 'Setup failed.':
            self.wait('Action: ', offset)
        if expected == 'Password saved.':
            self.wait('$ ', offset)

    def setup(self):
        self.wait('Action: ')
        self.enter('1', 'Username: ')

    def login(self, user, password, success=True):
        self.enter('console', 'Login: ')
        self.enter(user, 'Password: ')
        self.enter(password, user + '@root:~$ ' if success else 'Action: ')
        assert 'First-boot setup.' not in self.output()[self.output().rfind('HackOS login'):] or self.fresh

    def shell(self, text, expected):
        offset = len(self.output())
        self.enter(text, expected)
        self.wait('$ ', offset)

    def logout(self):
        self.enter('logout', 'Action: ')

    def screen(self, name):
        time.sleep(0.25) # allow idle to present the console after debug output
        self.qmp.call('screendump', {'filename': str(self.directory / (name + '.ppm'))})


@contextmanager
def boot(directory, image, firmware, name, timeout, *, write_error=False, entropy=True, reboot=False, serial=True, paused=False,
         qmp_stdio=False):
    with tempfile.TemporaryDirectory(prefix='hackos-accounts-') as temporary:
        temporary = Path(temporary)
        g = Guest()
        g.directory, g.timeout, g.fresh = directory, timeout, False
        g.console = directory / (name + '-console.log')
        g.serial = directory / (name + '-serial.log')
        qmp_path = temporary / 'qmp'
        g.gdb = temporary / 'gdb'
        drive = str(image)
        if write_error:
            fault = temporary / 'write-error.conf'
            fault.write_text('[inject-error]\nevent = "write_aio"\nerrno = "5"\nimmediately = "on"\nonce = "off"\n')
            drive = f'blkdebug:{fault}:{image}'
        args = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg',
                '-cpu', 'qemu64,+rdrand' if entropy else 'qemu64,-rdrand', '-m', '1G',
                '-smp', 'cores=4,threads=1,sockets=1', '-display', 'none', '-monitor', 'none',
                '-serial', f'file:{g.serial}' if serial else 'none', '-net', 'none',
                '-drive', f'file={drive},format=raw,werror=report',
                '-usb', '-device', 'usb-ehci,id=ehci', '-device', 'qemu-xhci,id=xhci',
                '-qmp', 'stdio' if qmp_stdio else f'unix:{qmp_path},server=on,wait=off',
                '-debugcon', f'file:{g.console}', '-global', 'isa-debugcon.iobase=0xe9']
        if not qmp_stdio:
            args += ['-gdb', f'unix:{g.gdb},server=on,wait=off']
        if paused:
            args += ['-S']
        if not reboot:
            args += ['-no-reboot']
        if firmware == 'uefi':
            ovmf = ROOT / 'boot/UEFI/OVMFbin'
            variables = temporary / 'OVMF_VARS.fd'
            shutil.copy2(ovmf / 'OVMF_VARS-pure-efi.fd', variables)
            args += ['-drive', f'if=pflash,format=raw,unit=0,file={ovmf}/OVMF_CODE-pure-efi.fd,readonly=on',
                     '-drive', f'if=pflash,format=raw,unit=1,file={variables}']
        with (directory / (name + '-qemu.log')).open('w') as log:
            g.process = subprocess.Popen(args, stdin=subprocess.PIPE if qmp_stdio else None,
                                         stdout=subprocess.PIPE if qmp_stdio else log, stderr=log, bufsize=0)
            g.qmp = None
            try:
                deadline = time.monotonic() + timeout
                while not qmp_stdio and not qmp_path.exists() and time.monotonic() < deadline:
                    assert g.process.poll() is None, 'QEMU could not start'
                    time.sleep(0.05)
                g.qmp = QMP(process=g.process) if qmp_stdio else QMP(qmp_path)
                yield g
            finally:
                if g.qmp:
                    g.qmp.close()
                g.process.terminate()
                try: g.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    g.process.kill()
                    g.process.wait()
                output = g.output() if g.console.exists() else ''
                for secret in (ADMIN_PASSWORD, ALICE_PASSWORD, CHANGED_PASSWORD, 'WrongPass123'):
                    assert secret not in output, 'Password leaked to console/debug log'
                    if serial and g.serial.exists():
                        assert secret not in g.serial.read_text(errors='replace'), 'Password leaked to serial log'


def inspect_slots(image, password):
    slots = read_accounts(image)
    for data in slots:
        assert len(data) == 8192 and data[:8] == b'HOSACCT1'
        assert struct.unpack_from('<II', data, 8) == (1, 5648)
        assert hashlib.sha256(data[:24] + bytes(32) + data[56:]).digest() == data[24:56]
        assert ADMIN_PASSWORD.encode() not in data and ALICE_PASSWORD.encode() not in data
    current = max(slots, key=lambda data: struct.unpack_from('<Q', data, 16)[0])
    assert current[128:160].split(b'\0')[0] == b'admin'
    # Root password is the first record after the 64-byte header and registry tables.
    offset = 64 + 16 + 32 * 80 + 32 * 40
    enabled, iterations = struct.unpack_from('<II', current, offset)
    salt, digest = current[offset + 8:offset + 24], current[offset + 24:offset + 56]
    assert enabled == 1 and iterations == 600000 and salt != bytes(16)
    assert hashlib.pbkdf2_hmac('sha256', password.encode(), salt, iterations) == digest
    return slots


def test(firmware, timeout):
    directory = OUT / ('qemu-accounts-' + firmware)
    report = Report('qemu-accounts-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'
    saved = []

    def fresh(name):
        destination = directory / (name + '.img')
        shutil.copy2(built / 'HackOS.img', destination)
        return destination

    def persistence():
        shutil.copy2(built / 'HackOS.img', image)
        for source, name in (('auth_probe.cpp', 'AUTHPROBE'), ('wait_probe.cpp', 'HOLDPROBE')):
            program = build_probe(directory, built / 'source', extra=(source, name))
            require_success(command(['mcopy', '-i', filesystem(image), program, '::/USR/BIN/' + name]), 'install probe')
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'setup', timeout) as g:
            g.fresh = True
            g.setup()
            assert 'Started user process 3' not in g.output()
            g.screen('first-boot')
            g.enter('admin', 'New password: ')
            g.enter('short', 'Confirm password: ')
            g.enter('short', 'Action: ')
            g.setup()
            g.enter('admin', 'New password: ')
            g.enter(ADMIN_PASSWORD, 'Confirm password: ')
            g.enter('Mismatch123', 'Action: ')
            g.setup()
            g.enter('BadName', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Setup failed.')
            g.setup()
            g.enter('admin', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('admin', 'WrongPass123', False)
            g.login('guest', ADMIN_PASSWORD, False)
            g.login('unknown', ADMIN_PASSWORD, False)
            g.login('admin', ADMIN_PASSWORD)
            g.shell('id', 'uid=0(admin) gid=0 groups=0')
            g.shell('user USR/BIN/AUTHPROBE', 'AUTH_BOUNDARIES_OK')
            g.shell('groupadd staff', 'Group created.')
            g.shell('useradd alice staff', 'User created.')
            g.shell('usermod alice users', 'Group added')
            g.enter('passwd alice', 'New password: ')
            g.password(ALICE_PASSWORD, 'Password saved.')
            g.wait('$ ')
            g.shell('user USR/BIN/HOLDPROBE &', 'WAIT_PROBE_READY')
            g.shell('runas alice /USR/BIN/HOLDPROBE &', 'WAIT_PROBE_READY')
            g.shell('window &', '$ ')
            # Wait for window creation/focus before clicking the console; a
            # fixed delay can race startup and discard the first logout keys.
            g.wait('Window demo ready.')
            g.qmp.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': -100}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': -100}},
                {'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
            g.qmp.call('input-send-event', {'events': [
                {'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
            # PS/2 mouse packets and QMP key injection are asynchronous. Verify
            # the focus handoff before typing, rather than racing its queue reset.
            g.qmp.call('stop')
            focus_script = directory / 'console-focus.gdb'
            focus_script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb, time\n' + """
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
bp.silent = True
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    gdb.execute('continue', to_string=True)
    if not int(gdb.parse_and_eval('KernelWindowManager->focused')) and not int(gdb.parse_and_eval('KernelWindowManager->previousButtons')):
        break
else: raise AssertionError('Console did not regain focus')
bp.delete()
print('CONSOLE_FOCUS_OK')
end
detach
quit
""")
            focus_result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', focus_script], timeout=90)
            (directory / 'console-focus.log').write_text(focus_result.stdout)
            require_success(focus_result, 'console focus handoff')
            assert 'CONSOLE_FOCUS_OK' in focus_result.stdout
            g.qmp.call('cont')
            g.logout()
            g.screen('login')
            # Idle must reclaim all session stacks, private pages, windows and handles.
            time.sleep(0.5)
            g.qmp.call('stop')
            script = directory / 'logout.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb\n' + '''
p = gdb.parse_and_eval('ready_queue')
count = 0
while int(p):
    assert int(p['pid']) in (1, 2), 'Session task survived logout'
    assert int(p['sessionId']) == 0
    count += 1
    p = p['next']
assert count == 2
assert int(gdb.parse_and_eval('terminalOwner')) == 2
assert int(gdb.parse_and_eval('user_windows_live')) == 0
assert int(gdb.parse_and_eval('KernelWindowManager->windows.count')) == 0
assert int(gdb.parse_and_eval('window_surface_pages')) == 0
assert int(gdb.parse_and_eval('user_pages_live')) == int(gdb.parse_and_eval('ready_queue->next->userSpace->ownedCount'))
print('LOGOUT_RECLAIMED_OK')
''' + 'end\ndetach\nquit\n')
            result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
            (directory / 'logout.log').write_text(result.stdout)
            require_success(result, 'inspect logout cleanup')
            assert 'LOGOUT_RECLAIMED_OK' in result.stdout
            g.qmp.call('cont')
            g.login('alice', ALICE_PASSWORD)
            g.shell('id', 'uid=1001(alice) gid=1001 groups=1001,1000')
            g.shell('user USR/BIN/AUTHPROBE', 'AUTH_BOUNDARIES_OK')
            g.shell('passwd', 'Permission denied: root required.')
            g.shell('shutdown', 'Permission denied: root required.')
            g.logout()
        inspect_slots(image, ADMIN_PASSWORD)
        # Stop/restart QEMU on exactly the same writable disk, without an overlay.
        with boot(directory, image, firmware, 'reboot', timeout) as g:
            g.wait('Action: ')
            assert 'First-boot setup.' not in g.output()
            g.login('alice', ALICE_PASSWORD)
            g.shell('id', 'uid=1001(alice) gid=1001 groups=1001,1000')
            g.logout()
            g.login('admin', ADMIN_PASSWORD)
            g.enter('passwd', 'New password: ')
            g.password(CHANGED_PASSWORD, 'Password saved.')
            g.wait('$ ')
            g.logout()
            g.login('admin', ADMIN_PASSWORD, False)
            g.login('admin', CHANGED_PASSWORD)
            g.shell('groupadd persisted', 'Group created.')
            g.logout()
            g.qmp.type('3\n')
            g.process.wait(timeout=timeout)
            assert g.process.returncode == 0 and 'Stopping the CPU. Bye!' in g.output()
        saved.extend(inspect_slots(image, CHANGED_PASSWORD))
        # Packaging carries exact bytes into a new filesystem, including damaged generations.
        repackaged = fresh('repackaged')
        write_accounts(repackaged, saved)
        assert read_accounts(repackaged) == saved
        with boot(directory, repackaged, firmware, 'repackaged', timeout) as g:
            g.wait('Action: ')
            assert 'First-boot setup.' not in g.output()
            g.login('admin', CHANGED_PASSWORD)
            g.shell('groups', 'persisted')
        return 'Setup validation, hidden passwords, independent PBKDF2 verification, reboot persistence, password change, non-root login, syscall boundaries, logout cleanup and repackaging passed.'

    def damaged_store():
        assert saved, 'Persistence prerequisite failed'
        newest = max(range(2), key=lambda n: struct.unpack_from('<Q', saved[n], 16)[0])
        slots = list(saved)
        bad = bytearray(slots[newest])
        bad[200] ^= 1
        slots[newest] = bytes(bad)
        recovery = fresh('recover')
        write_accounts(recovery, slots)
        with boot(directory, recovery, firmware, 'recover', timeout) as g:
            g.wait('Action: ')
            assert 'First-boot setup.' not in g.output()
            g.login('admin', CHANGED_PASSWORD)
            g.shell('groups', 'staff')
            assert 'persisted' not in g.output()
        for label, data in (('corrupt', [bytes(bad), bytes(bad)]),
                            ('interrupted-setup', [bytes(bad), bytes(8192)])):
            damaged = fresh(label)
            write_accounts(damaged, data)
            with boot(directory, damaged, firmware, label, timeout) as g:
                g.wait('Login is disabled')
                assert 'First-boot setup.' not in g.output() and '@root:' not in g.output()
                g.wait('Action: ')
                g.qmp.type('shutdown\n')
                g.process.wait(timeout=timeout)
                assert g.process.returncode == 0 and 'Stopping the CPU.' in g.output()
        return 'One damaged generation recovers; both damaged and interrupted first setup fail closed.'

    def missing_store():
        damaged = fresh('missing')
        fs = filesystem(damaged)
        require_success(command(['mattrib', '-i', fs, '-h', '-s', '::/SYSTEM/ACCT0.DAT']), 'unhide fixture')
        require_success(command(['mdel', '-i', fs, '::/SYSTEM/ACCT0.DAT']), 'remove fixture slot')
        with boot(directory, damaged, firmware, 'missing', timeout) as g:
            g.wait('Login is disabled')
            assert 'First-boot setup.' not in g.output()
        return 'Missing storage does not reset authentication.'

    def unavailable_entropy():
        empty = fresh('no-entropy')
        with boot(directory, empty, firmware, 'no-entropy', timeout, entropy=False) as g:
            g.setup()
            g.enter('admin', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Secure randomness unavailable; setup was not saved.')
            assert 'Setup complete.' not in g.output()
        assert read_accounts(empty) == [bytes(8192), bytes(8192)]
        return 'No RDRAND: no weak fallback and no saved credentials.'

    def failed_write():
        empty = fresh('write-error')
        with boot(directory, empty, firmware, 'write-error', timeout, write_error=True) as g:
            g.setup()
            g.enter('admin', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Setup failed.')
            g.wait('Login is disabled')
            assert 'Setup complete.' not in g.output()
        assert read_accounts(empty) == [bytes(8192), bytes(8192)]
        return 'Disk write failure is reported and freezes authentication.'

    def power_menu():
        for action, message, reason in (('2', 'Restarting the CPU.', 'guest-reset'),
                                         ('restart', 'Restarting the CPU.', 'guest-reset'),
                                         ('3', 'Stopping the CPU.', 'guest-shutdown'),
                                         ('shutdown', 'Stopping the CPU.', 'guest-shutdown')):
            empty = fresh('menu-' + action)
            with boot(directory, empty, firmware, 'menu-' + action, timeout, reboot=True) as g:
                g.wait('Action: ')
                assert '1) login\n2) restart\n3) shutdown' in g.output()
                if action == '3':
                    g.screen('logged-out-menu')
                g.enter('invalid', 'Action: ')
                assert 'Choose 1-3' in g.output() and 'Username:' not in g.output()
                offset = len(g.output())
                g.qmp.type(action + '\n')
                if reason == 'guest-reset':
                    g.wait('HackOS login', offset)
                    g.wait('Action: ', offset)
                    g.qmp.type('shutdown\n')
                g.process.wait(timeout=timeout)
                assert g.process.returncode == 0 and message in g.output()
                for raw in g.qmp.stream:
                    event = json.loads(raw)
                    if 'event' in event:
                        g.qmp.events.append(event)
                event_type = 'RESET' if reason == 'guest-reset' else 'SHUTDOWN'
                assert any(e.get('event') == event_type and e['data'].get('guest') and
                           e['data'].get('reason') == reason for e in g.qmp.events), g.qmp.events
            assert read_accounts(empty) == [bytes(8192), bytes(8192)]
        return 'Numeric and named logged-out restart/shutdown execute real guest power operations without creating an account.'

    for name, check in (('logged_out_power_menu', power_menu), ('persistent_accounts_and_sessions', persistence), ('damaged_store_recovery', damaged_store),
                        ('missing_store_fails_closed', missing_store), ('entropy_required', unavailable_entropy),
                        ('write_failure_fails_closed', failed_write)):
        report.check(name, check)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
