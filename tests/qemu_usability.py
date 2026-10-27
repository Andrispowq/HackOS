#!/usr/bin/env python3
"""Blocked input, job groups, private heaps and shell completion on both firmware paths."""
import argparse
import os
import re
import shutil
import sys
import time
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-usability-' + firmware)
    report = Report('qemu-usability-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def inspect(g, name, checks):
        g.qmp.call('stop')
        script = directory / (name + '.gdb')
        # Console writes can precede the next blocking syscall, and reaping
        # happens on idle. Await the actual state instead of sleeping a guessed
        # number of host milliseconds (especially under concurrent TCG runs).
        script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) + '\npython\nimport gdb, time\n' +
                          'checks = ' + repr(checks) + '\n' + """
failure = ''
def ready():
    global failure
    tasks = []
    p = gdb.parse_and_eval('ready_queue')
    while int(p):
        tasks.append(p.dereference())
        p = p['next']
    shell = next(p for p in tasks if int(p['systemShell']) and not int(p['authenticationAgent']))
    probes = [p for p in tasks if 'USABPROBE' in p['name'].string()]
    try:
        exec(checks, dict(gdb=gdb, tasks=tasks, shell=shell, probes=probes))
        return True
    except AssertionError as error:
        failure = str(error) or str([(int(p['pid']), int(p['state'])) for p in tasks])
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
print('USABILITY_STATE_OK')
end
detach
quit
""")
        result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
        (directory / (name + '.log')).write_text(result.stdout)
        require_success(result, name)
        assert 'USABILITY_STATE_OK' in result.stdout, result.stdout
        g.qmp.call('cont')

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('usability_probe.cpp', 'USABPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/USABPROBE']), 'install usability probe')
        fixture = directory / 'completion.txt'
        fixture.write_text('COMPLETION_FILE_OK\n')
        require_success(command(['mcopy', '-i', filesystem(image), fixture, '::/TMP/TWO WORDS.TXT']), 'completion fixture')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'usability', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            time.sleep(0.2)
            inspect(g, 'shell-blocked', '''
assert int(shell['state']) == 4, 'Idle shell is polling instead of blocking'
assert int(gdb.parse_and_eval('current_process->pid')) != int(shell['pid'])
''')
            g.shell('usabprobe', 'USABILITY_RUNTIME_OK 17')
            g.shell('usabprobe heapfault', 'fault 14')
            g.shell('usabprobe controller', 'USABILITY_CONTROLLER_OK')
            g.shell('runas guest usabprobe controller', 'USABILITY_CONTROLLER_OK')
            g.shell('usabprobe background &', 'USABILITY_BACKGROUND_DENIED')
            g.enter('usabprobe read', 'USABILITY_READ_READY')
            time.sleep(0.2)
            inspect(g, 'reader-blocked', '''
assert len(probes) == 1 and int(probes[0]['state']) == 4, [(int(p['pid']), int(p['state'])) for p in tasks]
assert int(shell['state']) == 3 and int(shell['waitingGroup']) == int(probes[0]['processGroup'])
assert int(gdb.parse_and_eval('current_process->pid')) != int(shell['pid'])
''')
            mark = len(g.output())
            g.qmp.key('k')
            g.wait('USABILITY_READ_OK', mark)
            g.wait('$ ', mark)
            mark = len(g.output())
            g.qmp.type('whoam')
            g.qmp.key('tab')
            g.enter('', 'root\n')
            g.wait('$ ', mark)
            mark = len(g.output())
            g.qmp.type('cat "/tmp/two')
            g.qmp.key('tab')
            g.enter('', 'COMPLETION_FILE_OK')
            g.wait('$ ', mark)
            g.qmp.type('cd /tm')
            g.qmp.key('tab')
            g.enter('', '@root:~/TMP$ ')
            mark = len(g.output())
            g.qmp.type('p')
            g.qmp.key('tab')
            g.wait('print  ', mark)
            g.wait('$ ', mark)
            mark = len(g.output())
            g.qmp.key('c', control=True)
            g.wait('^C', mark)
            g.wait('$ ', mark)
            g.shell('cd /', '@root:~$ ')
            g.enter('input', 'input> ')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('Stopped', mark)
            g.wait('$ ', mark)
            g.qmp.type('fg\n')
            time.sleep(0.3)
            g.enter('exit', '$ ')
            g.enter('usabprobe tree', 'USABILITY_CHILD_READY')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('Stopped', mark)
            g.wait('$ ', mark)
            inspect(g, 'tree-stopped', '''
assert len(probes) == 2 and all(int(p['state']) == 5 for p in probes)
assert len({int(p['processGroup']) for p in probes}) == 1
assert int(gdb.parse_and_eval('terminalOwner')) == int(shell['pid'])
assert all(int(p['userSpace'].dereference()['heapPages']) >= 3 for p in probes)
assert len({int(p['userSpace'].dereference()['heapFrames'][0]) for p in probes}) == 2, 'Heap frames shared across jobs'
for p in probes:
    entry = p['userSpace'].dereference()['leaf'].dereference()['entries'][256]
    assert int(entry['present']) and int(entry['writeable']) and int(entry['user_access']) and int(entry['execution_disabled'])
''')
            g.shell('jobs', 'Stopped')
            g.shell('bg', 'Running')
            time.sleep(0.2)
            inspect(g, 'tree-background', '''
assert len(probes) == 2 and all(int(p['state']) != 5 for p in probes)
assert int(gdb.parse_and_eval('terminalOwner')) == int(shell['pid'])
''')
            mark = len(g.output())
            g.qmp.type('fg\n')
            time.sleep(0.3)
            assert '$ ' not in g.output()[mark:]
            inspect(g, 'tree-foreground', '''
assert int(shell['state']) == 3
assert int(gdb.parse_and_eval('terminalOwner')) in [int(p['pid']) for p in probes]
''')
            g.qmp.key('c', control=True)
            g.wait('^C', mark)
            g.wait('$ ', mark)
            g.enter('usabprobe orphan', 'USABILITY_CHILD_READY')
            mark = len(g.output())
            time.sleep(0.2)
            assert '$ ' not in g.output()[mark:]
            g.qmp.key('z', control=True)
            g.wait('Stopped', mark)
            g.wait('$ ', mark)
            inspect(g, 'orphan-stopped', '''
assert len(probes) == 1 and int(probes[0]['state']) == 5
assert int(gdb.parse_and_eval('terminalOwner')) == int(shell['pid'])
''')
            text = g.enter('jobs', 'Stopped')
            group = int(re.search(r'\[(\d+)\] Stopped', text)[1])
            g.shell('kill %' + str(group), '$ ')
            g.shell('fg 999999', 'No such job.')
            g.shell('usabprobe child &', 'USABILITY_CHILD_READY')
            text = g.enter('jobs', 'Running')
            g.wait('$ ', len(g.output()) - len(text))
            group = int(re.search(r'\[(\d+)\] Running', text)[1])
            mark = len(g.output())
            g.qmp.type('wait\n')
            g.wait('wait\n', mark)
            inspect(g, 'wait-blocked', "assert int(shell['state']) == 3")
            assert '$ ' not in g.output()[mark:]
            g.qmp.key('c', control=True)
            g.wait('^C', mark)
            g.wait('$ ', mark)
            g.shell('jobs', 'Running')
            g.shell('kill %' + str(group), '$ ')
            # An explicitly launched nested shell is an ordinary process but
            # still owns the separate jobs it creates.
            mark = len(g.output())
            g.enter('shell', "HackOS ring-3 shell.")
            g.wait("$ ", mark)
            g.shell('print nested', 'nested\n')
            g.shell('logout', '$ ')
            # Exercise explicit wait and completion after idle has reaped a child.
            g.shell('print completed &', '$ ')
            g.shell('wait', '$ ')
            time.sleep(0.4)
            inspect(g, 'resources-reclaimed', '''
assert not probes and len(tasks) == 3, [(int(p['pid']), int(p['state']), p['name'].string()) for p in tasks]
assert int(gdb.parse_and_eval('user_files_live')) == 0
assert int(gdb.parse_and_eval('user_windows_live')) == 0
assert int(gdb.parse_and_eval('user_pages_live')) == sum(int(p['userSpace'].dereference()['ownedCount']) for p in tasks if int(p['userSpace']))
assert int(gdb.parse_and_eval('terminalOwner')) == int(shell['pid'])
assert int(shell['state']) == 4
''')
            g.screen('usability')
            # Logout also kills stopped groups and releases their heaps/files.
            g.enter('usabprobe tree', 'USABILITY_CHILD_READY')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('$ ', mark)
            g.logout()
            g.login('root', ADMIN_PASSWORD)
            time.sleep(0.4)
            inspect(g, 'logout-reclaimed', '''
assert not probes and len(tasks) == 3, [(int(p['pid']), int(p['state']), p['name'].string()) for p in tasks]
assert int(gdb.parse_and_eval('user_files_live')) == 0
assert int(gdb.parse_and_eval('user_pages_live')) == sum(int(p['userSpace'].dereference()['ownedCount']) for p in tasks if int(p['userSpace']))
''')
        return 'Real PS/2 input, blocked scheduler states, whole-job stop/continue/kill, orphan cleanup, heap NX/bounds/reclamation and completion checked.'

    report.check('userspace_usability', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', required=True, choices=('bios', 'uefi'))
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
