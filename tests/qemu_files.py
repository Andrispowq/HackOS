#!/usr/bin/env python3
"""Step-15 file utilities/editor, safe ABI and reboot persistence on disposable disks."""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe


def test(firmware, timeout):
    directory = OUT / ('qemu-files-' + firmware)
    report = Report('qemu-files-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def read(path):
        return require_success(command(['mtype', '-i', filesystem(image), '::' + path]), 'read saved file')

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('file_probe.cpp', 'FILEPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/FILEPROBE']), 'install file probe')
        for name, data in [('HEX.BIN', bytes([0, 1, 127, 255, 65, 10])), ('READONLY.TXT', b'locked\n'),
                           ('LARGE.TXT', b'a' * 32769)]:
            fixture = directory / name
            fixture.write_bytes(data)
            require_success(command(['mcopy', '-i', filesystem(image), fixture, '::/' + name]), 'install fixture')
        require_success(command(['mattrib', '-i', filesystem(image), '+r', '::/READONLY.TXT']), 'readonly fixture')
        from session import provision_imports
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'files', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('fileprobe', 'FILE_ABI_OK')
            g.wait('FILE_READER_READY')
            g.shell('rm /files/held.txt', 'rm: failed')
            g.shell('mv /files/held.txt /held.txt', 'mv: failed')
            holder = re.findall(r'Started user process (\d+): ~/USR/BIN/FILEPROBE', g.output())[-1]
            g.shell('kill ' + holder, '$ ')
            time.sleep(0.3)
            g.shell('rm /files/held.txt', '$ ')
            g.shell('cat /files/held.txt', 'File not found.')
            g.shell('xxd /hex.bin', '00000000: 00 01 7f ff  00000000 00000001 01111111 11111111 |....|')
            g.shell('xxd /large.txt', 'at most 32768 bytes')
            g.shell('xxd /absent', 'xxd: cannot read file.')
            g.shell('mkdir', 'Usage: mkdir <path>')
            g.shell('mkdir /notes', '$ ')
            g.shell('mkdir /notes', 'mkdir: failed')
            g.shell('mkdir /absent/child', 'mkdir: failed')
            g.shell('mkdir ""', 'mkdir: failed')
            g.shell('rm /notes', 'rm: failed')
            g.shell('mv /notes /other', 'mv: failed')
            g.shell('rm /readonly.txt', 'rm: failed')
            g.shell('edit /large.txt', 'edit: cannot load file')
            g.shell('edit /hex.bin', 'edit: cannot load file')
            g.shell('edit /system/acct0.dat', 'edit: cannot load file')
            g.shell('edit /notes/bg.txt &', 'edit: foreground terminal required.')
            g.enter('edit /notes/new.txt', 'New file. Not saved yet.')
            g.qmp.type('hello\nworld')
            g.qmp.key('left')
            g.qmp.key('left')
            g.qmp.type('X')
            g.qmp.key('delete')
            g.qmp.key('up')
            g.qmp.type('X') # hello -> hellXo, column four on previous line
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Saved.', offset)
            g.screen('editor-saved')
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('edit: exited.', offset)
            g.wait('$ ', offset)
            g.shell('cat /notes/new.txt', 'hellXo\nworXd')
            # A second save must shrink the file, without leftover old bytes.
            g.enter('edit /notes/new.txt', 'File loaded.')
            for _ in range(12):
                g.qmp.key('delete')
            g.qmp.type('short\n')
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Saved.', offset)
            g.qmp.type('discard me')
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('unsaved changes discarded.', offset)
            g.wait('$ ', offset)
            g.shell('cat /notes/new.txt', 'short\n')
            g.shell('mv /notes/new.txt /files/a.txt', 'mv: failed')
            g.shell('cat /files/a.txt', 'original')
            g.shell('mv /notes/new.txt "/files/long saved name.txt"', '$ ')
            g.shell('cat /notes/new.txt', 'File not found.')
            g.shell('cat "/files/long saved name.txt"', 'short\n')
            g.enter('edit /readonly.txt', 'File loaded.')
            g.qmp.type('X')
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Save failed. Buffer kept', offset)
            g.screen('editor-save-failed')
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('$ ', offset)
            g.enter('edit /files/empty.txt', 'New file.')
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Saved.', offset)
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('$ ', offset)
            g.shell('xxd /files/empty.txt', 'OFFSET')
            g.shell('runas guest mkdir /tmp/unpriv', '$ ')
            g.enter('runas guest edit /tmp/unpriv/note.txt', 'New file.')
            g.qmp.type('user text')
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Saved.', offset)
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('root@root:~$ ', offset)
            g.shell('rm /files/b.txt', '$ ')
            g.shell('rm /files/b.txt', 'rm: failed')
            g.screen('file-commands')
            # All program-owned read handles and private pages should be reclaimed.
            time.sleep(0.5)
            g.qmp.call('stop')
            script = directory / 'cleanup.gdb'
            script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb\n'
                              'assert int(gdb.parse_and_eval("user_files_live")) == 0\n'
                              'p = gdb.parse_and_eval("ready_queue")\ncount = 0\n'
                              'while int(p):\n count += 1\n p = p["next"]\n'
                              'assert count == 3, count\nprint("FILE_CLEANUP_OK")\nend\ndetach\nquit\n')
            result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
            (directory / 'cleanup.log').write_text(result.stdout)
            require_success(result, 'file command cleanup')
            assert 'FILE_CLEANUP_OK' in result.stdout
            g.qmp.call('cont')
        assert read('/FILES/long saved name.txt') == 'short\n'
        assert read('/FILES/EMPTY.TXT') == ''
        assert read('/TMP/UNPRIV/NOTE.TXT') == 'user text'
        assert read('/READONLY.TXT') == 'locked\n'
        assert read('/FILES/A.TXT') == 'original'
        assert read('/FILES/LIMIT.TXT') == ''.join(chr(97 + i % 26) for i in range(32768))
        missing = command(['mdir', '-i', filesystem(image), '::/FILES/B.TXT'])
        assert missing.returncode != 0
        with boot(directory, image, firmware, 'reboot', timeout) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('cat "/files/long saved name.txt"', 'short\n')
            g.shell('ls /notes', '$ ')
            g.shell('cat /files/b.txt', 'File not found.')
            g.shell('xxd /files/empty.txt', 'OFFSET')
            g.screen('after-reboot')
        # A real device write error must reach the editor, which keeps editing
        # and can return to the shell. Use a separate copy of the saved disk.
        fault = directory / 'write-error.img'
        shutil.copy2(image, fault)
        with boot(directory, fault, firmware, 'write-error', timeout, write_error=True) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.enter('edit /files/a.txt', 'File loaded.')
            g.qmp.type('X')
            offset = len(g.output())
            g.qmp.key('s', control=True)
            g.wait('Save failed. Buffer kept', offset)
            g.screen('disk-save-failed')
            offset = len(g.output())
            g.qmp.key('x', control=True)
            g.wait('$ ', offset)
        assert require_success(command(['mtype', '-i', filesystem(fault), '::/FILES/A.TXT']), 'original after failed save') == 'original'

    report.check('file_utilities_editor_persistence_and_errors', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
