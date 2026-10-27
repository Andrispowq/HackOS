#!/usr/bin/env python3
"""Standard streams, pipelines, deadlines and COM1 diagnostics on fresh guest images."""
import argparse
import os
import re
import shutil
import sys
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-streams-' + firmware)
    report = Report('qemu-streams-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def inspect(g, name, checks):
        g.qmp.call('stop')
        script = directory / (name + '.gdb')
        script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb, time\nchecks = ' + repr(checks) + '\n' + """
failure = ''
def ready():
    global failure
    tasks = []
    p = gdb.parse_and_eval('ready_queue')
    while int(p):
        tasks.append(p.dereference())
        p = p['next']
    shell = next(p for p in tasks if int(p['systemShell']) and not int(p['authenticationAgent']))
    children = [p for p in tasks if not int(p['systemShell']) and int(p['userSpace'])]
    try:
        exec(checks, dict(gdb=gdb, tasks=tasks, shell=shell, children=children))
        return True
    except AssertionError as error:
        failure = str(error) or str([(int(p['pid']), int(p['state']), p['name'].string()) for p in tasks])
        return False
class Milestone(gdb.Breakpoint):
    def stop(self):
        return ready() or time.monotonic() >= deadline
if not ready():
    deadline = time.monotonic() + 15
    bp = Milestone('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute('continue')
    bp.delete()
assert ready(), failure
print('STREAM_STATE_OK')
end
detach
quit
""")
        result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
        (directory / (name + '.log')).write_text(result.stdout)
        require_success(result, name)
        assert 'STREAM_STATE_OK' in result.stdout, result.stdout
        g.qmp.call('cont')

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('streams_probe.cpp', 'STREAMPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/STREAMPROBE']), 'install streams probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'streams', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('streamprobe', 'STREAM_RUNTIME_OK')
            g.shell('print first>"/tmp/two words"', '$ ')
            g.shell('print second>>"/tmp/two words"', '$ ')
            g.shell('cat<"/tmp/two words"|cat', 'first\nsecond\n')
            g.shell('streamprobe source|cat|cat> /tmp/binary.dat', '$ ')
            g.shell('streamprobe both>/tmp/out 2>/tmp/err', '$ ')
            g.shell('streamprobe both>/tmp/both 2>&1', '$ ')
            g.shell('streamprobe both 2>&1>/tmp/ordered', 'STREAM_STDERR\n')
            g.shell('cat /missing 2>/tmp/error', '$ ')
            g.shell('history>/tmp/history', '$ ')
            g.shell('cd /missing 2>/tmp/cderror', '$ ')
            g.shell('print private>/tmp/private', '$ ')
            g.shell('chmod 600 /tmp/private', '$ ')
            g.shell('runas guest cat /tmp/private 2>/tmp/denied', '$ ')
            g.shell('streamprobe both>/SYSTEM/ACCT0.DAT', 'Cannot open redirection')
            g.shell('print untouched>/tmp/private 2>/missing/path', 'Pipeline could not start')
            # Redirection truncation is intentionally observable on later failure.

            g.shell('print "a|b>c&d"', 'a|b>c&d\n')
            g.shell('print x||cat', 'Invalid arguments')
            g.shell('print x >', 'Invalid arguments')
            g.shell('print held|cat 2>/missing/path', 'Pipeline could not start')
            g.shell('cd /tmp|cat', 'Shell built-ins require')
            g.shell('print background>/tmp/background &', 'Running')
            g.shell('wait', '$ ')
            g.shell('streamprobe fault|cat', 'fault 14')
            mark = len(g.output())
            g.qmp.type('cat\n')
            inspect(g, 'terminal-stream', "assert len(children) == 1 and int(children[0]['state']) == 6")
            g.qmp.type('stdin_bytes')
            g.wait('stdin_bytes', mark)
            g.qmp.key('d', control=True)
            g.wait('$ ', mark)
            mark = len(g.output())
            g.qmp.type('print x|whoam')
            g.qmp.key('tab')
            g.qmp.type('\n')
            g.wait('USR/BIN/WHOAMI', mark)
            g.wait('$ ', mark)
            visible = re.sub(r'(?:Started user process [^\n]*|User process [^\n]*)\n', '', g.output()[mark:])
            assert 'root\n' in visible, visible

            # Both stages block, one on poll and one on an empty pipe. A stopped
            # group retains both endpoints; Ctrl-C releases them without a leak.
            g.enter('streamprobe hang|cat', 'STREAM_BLOCK_READY')
            inspect(g, 'blocked', '''
assert len(children) == 2 and all(int(p['state']) == 6 for p in children)
assert int(shell['state']) == 3
assert len({int(p['processGroup']) for p in children}) == 1
assert int(gdb.parse_and_eval('user_pipes_live')) == 1
assert all(int(gdb.parse_and_eval('current_process->pid')) != int(p['pid']) for p in children)
''')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('Stopped', mark)
            g.wait('$ ', mark)
            inspect(g, 'stopped', "assert len(children) == 2 and all(int(p['state']) == 5 for p in children)")
            g.shell('bg', 'Running')
            mark = len(g.output())
            g.qmp.type('fg\n')
            inspect(g, 'resumed', "assert len(children) == 2 and int(shell['state']) == 3 and all(int(p['state']) == 6 for p in children)")
            g.qmp.key('c', control=True)
            g.wait('^C', mark)
            g.wait('$ ', mark)
            g.shell('streamprobe hang|cat &', 'STREAM_BLOCK_READY')
            output = g.enter('jobs', 'Running')
            g.wait('$ ', len(g.output()) - len(output))
            group = int(re.search(r'\[(\d+)\] Running', output)[1])
            g.shell('kill %' + str(group), '$ ')
            inspect(g, 'cleanup', '''
assert not children and len(tasks) == 3
assert int(gdb.parse_and_eval('user_streams_live')) == 0
assert int(gdb.parse_and_eval('user_pipes_live')) == 0
assert int(gdb.parse_and_eval('user_files_live')) == 0
assert all(not int(shell['descriptors'][i]) for i in range(3,16))
assert all(not int(shell['children'][i]['pid']) for i in range(16))
''')
            g.screen('streams')
            serial = g.serial.read_text(errors='replace')
            assert f'HackOS {firmware.upper()} loader' in serial, serial[:1000]
            assert f'HackOS kernel entry: {firmware.upper()}' in serial
            assert 'STREAM_RUNTIME_OK' in serial and 'fault 14' in serial
            assert ADMIN_PASSWORD not in serial
            g.enter('streamprobe hang|cat', 'STREAM_BLOCK_READY')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('$ ', mark)
            g.logout()
            g.login('root', ADMIN_PASSWORD)
            inspect(g, 'logout-cleanup', "assert not children and int(gdb.parse_and_eval('user_pipes_live')) == 0 and int(gdb.parse_and_eval('user_streams_live')) == 0")
        # Inspect bytes independently of the live FAT cache.
        def contents(path):
            result = command(['mcopy', '-o', '-i', filesystem(image), '::' + path, directory / 'extracted'])
            require_success(result, path)
            return (directory / 'extracted').read_bytes()
        assert contents('/TMP/TWO WORDS') == b'first\nsecond\n'
        assert contents('/TMP/BINARY.DAT') == bytes(i % 251 for i in range(12 * 1024))
        assert contents('/TMP/OUT') == b'STREAM_STDOUT\n'
        assert contents('/TMP/ERR') == b'STREAM_STDERR\n'
        assert contents('/TMP/BOTH') == b'STREAM_STDOUT\nSTREAM_STDERR\n'
        assert contents('/TMP/ORDERED') == b'STREAM_STDOUT\n'
        assert contents('/TMP/ERROR') == b'File not found.\n'
        assert b'history>/tmp/history' in contents('/TMP/HISTORY')
        assert contents('/TMP/BACKGROUND') == b'background\n'
        assert contents('/TMP/STREAMS.DAT') == b'ABC'
        assert contents('/TMP/DENIED') == b'File not found.\n'
        assert contents('/TMP/CDERROR') == b'Directory not found.\n'
        assert contents('/TMP/PRIVATE') == b''
        with boot(directory, image, firmware, 'no-uart', timeout, serial=False) as g:
            g.wait('Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('cat /tmp/out', 'STREAM_STDOUT\n')
        return 'Binary pipes exceed buffer capacity; descriptors, deadlines, EOF/broken pipes, ordered redirects, job lifecycle, serial boot/fault logs, absent UART and persisted bytes verified.'

    report.check('streams_pipes_timeouts_serial', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=('bios', 'uefi'))
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
