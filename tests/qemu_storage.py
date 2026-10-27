#!/usr/bin/env python3
"""Storage modes, permissions and interrupted journal replay on disposable images."""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-storage-' + firmware)
    report = Report('qemu-storage-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'
    offset = 846 * 512 if firmware == 'bios' else 0

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('storage_probe.cpp', 'STORPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/STORPROBE']), 'install storage probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'permissions', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('storprobe', 'STORAGE_ROOT_OK')
            g.shell('runas guest storprobe guest', 'STORAGE_GUEST_OK')
            g.shell('chmod 755 /usr/bin/echo', '$ ')
            g.shell('chown guest users /tmp/new', '$ ')
            g.shell('ls -l /tmp', '-rw-r----- 1000 1000')
            g.screen('permissions')
        with boot(directory, image, firmware, 'reboot', timeout) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('runas guest storprobe reboot', 'STORAGE_REBOOT_OK')
            # Stop precisely after the prepared journal has been flushed and
            # before replaying any home sectors. The next boot must replay it.
            g.qmp.type('mv /tmp/new /tmp/recovered')
            g.qmp.call('stop')
            marker = directory / 'prepared.marker'
            marker.unlink(missing_ok=True)
            script = directory / 'interrupt.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\n'
                              'break JournalDevice::Replay\ncontinue\npython\n'
                              'from pathlib import Path\nPath(' + repr(str(marker)) + ').write_text("prepared")\n'
                              'end\nwhile 1\n shell sleep 1\nend\n')
            with (directory / 'interrupt.log').open('w') as log:
                debugger = subprocess.Popen([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], stdout=log, stderr=subprocess.STDOUT)
                try:
                    time.sleep(0.5)
                    g.qmp.type('\n')
                    deadline = time.monotonic() + timeout
                    while not marker.exists() and time.monotonic() < deadline:
                        assert debugger.poll() is None, 'debugger exited before journal breakpoint'
                        time.sleep(0.05)
                    assert marker.exists(), 'journal breakpoint not reached'
                    g.process.kill()
                    g.process.wait(timeout=5)
                finally:
                    debugger.terminate()
                    debugger.wait(timeout=5)
        with image.open('rb') as stream:
            stream.seek(offset + 8 * 512)
            header = stream.read(512)
        assert header[:8] == b'HKFSJ016' and struct.unpack_from('<I', header, 12)[0] > 0
        with boot(directory, image, firmware, 'recovery', timeout) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('cat /tmp/recovered', 'created')
            g.shell('cat /tmp/new', 'File not found.')
            g.shell('ls -l /tmp', '-rw-r--r-- 1000 1000')
            g.screen('recovered')
        assert require_success(command(['mtype', '-i', filesystem(image), '::/TMP/RECOVERED']), 'persisted rename') == 'created'
        assert command(['mdir', '-i', filesystem(image), '::/TMP/NEW']).returncode != 0
        with image.open('rb') as stream:
            stream.seek(offset + 8 * 512 + 12)
            assert stream.read(4) == bytes(4), 'journal not cleared after replay'

    def full_volume():
        fault = directory / 'full.img'
        shutil.copy2(image, fault)
        with fault.open('r+b') as stream:
            stream.seek(offset)
            header = stream.read(512)
            reserved = struct.unpack_from('<H', header, 14)[0]
            fat_size = struct.unpack_from('<I', header, 36)[0]
            total = struct.unpack_from('<I', header, 32)[0]
            clusters = (total - reserved - header[16] * fat_size) // header[13]
            for copy in range(header[16]):
                position = offset + (reserved + copy * fat_size) * 512
                stream.seek(position)
                table = bytearray(stream.read(fat_size * 512))
                for cluster in range(2, clusters + 2):
                    if struct.unpack_from('<I', table, cluster * 4)[0] & 0xfffffff == 0:
                        struct.pack_into('<I', table, cluster * 4, 0xfffffff)
                stream.seek(position)
                stream.write(table)
        with boot(directory, fault, firmware, 'full', timeout) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.enter('runas guest edit /tmp/full', 'New file.')
            g.qmp.type('no space')
            previous = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Save failed. Buffer kept', previous)
            g.screen('full-volume')
            previous = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('$ ', previous)
            g.shell('cat /tmp/recovered', 'created')
            g.shell('chmod 600 /tmp/recovered', '$ ')
        assert command(['mdir', '-i', filesystem(fault), '::/TMP/FULL']).returncode != 0

    report.check('permissions_handles_reboot_and_interrupted_rename', run)
    report.check('full_volume_preserves_files_and_reports_failure', full_volume)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=('bios', 'uefi'))
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
