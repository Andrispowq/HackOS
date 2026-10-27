#!/usr/bin/env python3
"""Normal-user filters, four-stage pipes, descriptor query and foreground paging."""
import argparse
import os
import shutil
import sys
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-filters-' + firmware)
    report = Report('qemu-filters-' + firmware, directory)
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
    children = [p for p in tasks if int(p['userSpace']) and not int(p['systemShell'])]
    try:
        exec(checks, dict(gdb=gdb, tasks=tasks, children=children))
        return True
    except AssertionError as error:
        failure = str(error) or str([(int(p['pid']), int(p['state'])) for p in children])
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
print('FILTER_STATE_OK')
end
detach
quit
""")
        result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=20)
        (directory / (name + '.log')).write_text(result.stdout)
        require_success(result, name)
        assert 'FILTER_STATE_OK' in result.stdout, result.stdout
        g.qmp.call('cont')

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('filters_probe.cpp', 'FILTERPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/FILTERPROBE']), 'install probe')
        fixture = b'Alpha one\nbeta TWO\nalpha three\nlast'
        pages = b'one\ntwo\nthree\nfour\nfive\nsix\nseven\n'
        for name, content in [('filter.txt', fixture), ('pages.txt', pages)]:
            source = directory / name
            source.write_bytes(content)
            require_success(command(['mcopy', '-i', filesystem(image), source, '::/USR/' + name]), name)
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'filters', timeout) as g:
            def shell(text, expected):
                mark = len(g.output())
                g.shell(text, expected)
                if expected == '$ ':
                    output = g.output()[mark:]
                    for failure in ('Pipeline could not start', 'Rejected user program', 'Program exited with status'):
                        assert failure not in output, output

            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            shell('filterprobe', 'FILTER_ABI_OK')
            shell('echo secret > /tmp/private', '$ ')
            shell('chmod 600 /tmp/private', '$ ')
            g.enter('passwd guest', 'New password: ')
            g.password('GuestPass123', 'Password saved.')
            g.logout()
            g.login('guest', 'GuestPass123')
            shell('filterprobe', 'FILTER_ABI_OK')
            shell('filterprobe background &', 'FILTER_BACKGROUND_DENIED')
            shell('wait', '$ ')
            shell('filterprobe children | more', 'FILTER_CHILD_DENIED')
            shell('grep -in alpha /usr/filter.txt > /tmp/matches', '$ ')
            shell('grep "^alpha.*ee$" /usr/filter.txt > /tmp/pattern', '$ ')
            shell('grep -F missing /usr/filter.txt', 'Program exited with status 1.')
            shell('grep "[bad" /usr/filter.txt', 'Program exited with status 2.')
            shell('grep -c a /usr/filter.txt /usr/pages.txt > /tmp/counts', '$ ')
            shell('wc /usr/filter.txt > /tmp/wc', '$ ')
            shell('head -n2 < /usr/filter.txt > /tmp/head', '$ ')
            shell('tail -n2 /usr/filter.txt > /tmp/tail', '$ ')
            shell('tail -c +7 /usr/filter.txt > /tmp/skip', '$ ')
            shell('cat /usr/filter.txt | tee /tmp/copy | wc -c > /tmp/bytes', '$ ')
            shell('echo extra | tee -a /tmp/copy > /tmp/extra', '$ ')
            shell('filterprobe source | grep -F row | tail -n2 | wc -l > /tmp/chain', '$ ')
            shell('filterprobe binary | tee /tmp/bin | more > /tmp/bin2', '$ ')
            shell('more /usr/filter.txt | cat > /tmp/more', '$ ')
            shell('more /usr/filter.txt > /tmp/morebg &', 'Running')
            shell('wait', '$ ')
            shell('grep x /tmp/private 2> /tmp/denied', 'Program exited with status 2.')
            shell('echo x | tee /tmp/private /tmp/saved > /tmp/teeout 2> /tmp/teeerr', 'Program exited with status 1.')
            shell('filterprobe source | head -n1 > /tmp/early', '$ ')
            shell('filterprobe source | grep -q row0000', '$ ')
            shell('more -n2 /usr/pages.txt &', 'foreground terminal required')
            shell('wait', '$ ')

            # Pager data comes from a pipe, keys from the foreground terminal.
            mark = len(g.output())
            g.enter('cat /usr/pages.txt | more -n2', '--More--')
            assert 'one\ntwo\n--More--' in g.output()[mark:]
            assert '\nthree\n' not in g.output()[mark:]
            mark = len(g.output())
            g.qmp.key('ret')
            g.wait('three\n--More--', mark)
            mark = len(g.output())
            g.qmp.key('spc')
            g.wait('four\nfive\n--More--', mark)
            g.screen('pager')
            mark = len(g.output())
            g.qmp.type('q')
            g.wait('$ ', mark)

            # Upstream remains alive with a full pipe while more reads keys.
            g.enter('filterprobe source | more -n2', '--More--')
            mark = len(g.output())
            g.qmp.key('z', control=True)
            g.wait('Stopped', mark)
            g.wait('$ ', mark)
            mark = len(g.output())
            g.qmp.type('fg\n')
            # Wait for actual input ownership and a blocked pager; sending a key
            # immediately after Enter can race the foreground handoff/queue clear.
            inspect(g, 'resumed', """
assert len(children) == 2
pager = next((p for p in children if p['name'].string().endswith('/MORE')), None)
owner = next((p for p in children if int(p['pid']) == int(gdb.parse_and_eval('terminalOwner'))), None)
assert pager is not None and owner is not None
assert int(pager['state']) == 4 and int(pager['pipelineMember'])
assert int(owner['pipelineMember']) and int(owner['processGroup']) == int(pager['processGroup'])
""")
            g.qmp.type(' ')
            g.wait('row0002\nrow0003\n--More--', mark)
            g.screen('resumed-pager')
            mark = len(g.output())
            g.qmp.type('q')
            g.wait('$ ', mark)
            shell('echo after-pager | wc -l', '\n1\n')

            # Giving pagers group input access must keep ordinary tty pipelines working.
            mark = len(g.output())
            g.qmp.type('cat | wc -c\n')
            g.wait('USR/BIN/WC', mark)
            g.qmp.type('abc')
            g.qmp.key('d', control=True)
            g.wait('\n3\n', mark)
            g.wait('$ ', mark)
            inspect(g, 'cleanup', """
assert not children and len(tasks) == 3
assert int(gdb.parse_and_eval('user_pipes_live')) == 0
assert int(gdb.parse_and_eval('user_streams_live')) == 0
assert int(gdb.parse_and_eval('user_files_live')) == 0
""")
            shell('ps', '$ ')
            assert 'Exception:' not in g.output()

        def read(name):
            target = directory / 'extracted'
            require_success(command(['mcopy', '-o', '-i', filesystem(image), '::/TMP/' + name, target]), name)
            return target.read_bytes()

        expected = {'matches': b'1:Alpha one\n3:alpha three\n', 'pattern': b'alpha three\n',
                    'counts': b'/usr/filter.txt:4\n/usr/pages.txt:0\n', 'wc': b'3 7 35 /usr/filter.txt\n',
                    'head': b'Alpha one\nbeta TWO\n', 'tail': b'alpha three\nlast', 'skip': fixture[6:],
                    'copy': fixture + b'extra\n', 'extra': b'extra\n', 'bytes': b'35\n', 'chain': b'2\n',
                    'more': fixture, 'morebg': fixture, 'early': b'row0000\n', 'saved': b'x\n', 'teeout': b'x\n'}
        for name, content in expected.items():
            assert read(name) == content, (name, read(name), content)
        assert b'cannot open' in read('denied') and b'cannot open' in read('teeerr')
        assert read('private') == b'secret\n'
        assert read('bin') == read('bin2') == bytes(i % 251 for i in range(12 * 1024))
        return 'All six tools as guest; exact saved bytes, errors, four stages, early exits, Space/Enter/q and stop/resume.'

    report.check('filters', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
