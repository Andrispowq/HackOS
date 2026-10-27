#!/usr/bin/env python3
"""Normal-user sort/uniq/cut/tr, four-stage pipes, redirection and failure paths."""
import argparse
import shutil
import sys
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-text-tools-' + firmware)
    report = Report('qemu-text-tools-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('filters_probe.cpp', 'FILTERPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/FILTERPROBE']), 'install probe')
        records = b'beta:2\nalpha:10\nbeta:2\nAlpha:3\nplain\nalpha:10'
        source = directory / 'records.txt'
        source.write_bytes(records)
        require_success(command(['mcopy', '-i', filesystem(image), source, '::/USR/RECORDS.TXT']), 'install records')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'text-tools', timeout, qmp_stdio=True) as g:
            def shell(text, expected='$ '):
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
            shell('echo secret > /tmp/private')
            shell('chmod 600 /tmp/private')
            g.enter('passwd guest', 'New password: ')
            g.password('GuestPass123', 'Password saved.')
            g.logout()
            g.login('guest', 'GuestPass123')
            shell('id', 'uid=')
            shell('cut -d: -f1 /usr/records.txt | tr A-Z a-z | sort | uniq -c > /tmp/freq')
            shell('sort -u /usr/records.txt > /tmp/sorted')
            shell('cut -s -d: -f2 /usr/records.txt | sort -nr > /tmp/numeric')
            shell('uniq -d /tmp/sorted > /tmp/repeated')
            shell('uniq /tmp/numeric /tmp/unique')
            shell('tr -d "[:digit:]" < /tmp/numeric > /tmp/deleted')
            shell('echo "a   b" | tr -s "[:space:]" >> /tmp/deleted')
            shell('tr "[:upper:]" "[:lower:]" < /usr/records.txt > /tmp/lower')
            shell('cut -c1-2 /usr/records.txt > /tmp/columns')
            shell('sort /usr/records.txt > /tmp/sortbg &', 'Running')
            shell('wait')
            for name, args in [('sort', ''), ('uniq', ''), ('cut', '-b1 ')]:
                shell(name + ' ' + args + '/tmp/private 2> /tmp/' + name + 'err', 'Program exited with status 1.')
                shell(name + ' --bad', 'Program exited with status 2.')
            shell('tr z-a x', 'Program exited with status 2.')
            shell('cut -b0 /usr/records.txt', 'Program exited with status 2.')
            shell('uniq /usr/records.txt /tmp/private 2> /tmp/uniqout', 'Program exited with status 1.')
            # More than a pipe buffer, including early consumer exit in every tool.
            shell('filterprobe source | sort -r | head -n1 > /tmp/sortpipe')
            shell('filterprobe source | uniq | head -n1 > /tmp/uniqpipe')
            shell('filterprobe source | cut -c4- | head -n1 > /tmp/cutpipe')
            shell('filterprobe source | tr a-z A-Z | head -n1 > /tmp/trpipe')
            shell(r'filterprobe binary | tr -d "\\000" | wc -c > /tmp/binbytes')
            shell(r'filterprobe binary | tr "\\000" X > /tmp/trbin')
            shell('echo background | tr a-z A-Z > /tmp/trbg &', 'Running')
            shell('wait')
            # Repeated jobs must release enough tasks and descriptors to keep launching.
            for _ in range(12):
                shell('echo ok | sort | uniq | cut -c1- > /tmp/reuse')
            shell('cat /tmp/reuse', '\nok\n')
            assert 'Exception:' not in g.output()

        def read(name):
            target = directory / 'extracted'
            require_success(command(['mcopy', '-o', '-i', filesystem(image), '::/TMP/' + name, target]), name)
            return target.read_bytes()

        expected = {'freq': b'      3 alpha\n      2 beta\n      1 plain\n',
                    'sorted': b'Alpha:3\nalpha:10\nbeta:2\nplain\n',
                    'numeric': b'10\n10\n3\n2\n2\n', 'repeated': b'', 'unique': b'10\n3\n2\n',
                    'deleted': b'\n\n\n\n\na b\n', 'lower': records.lower(),
                    'columns': b'be\nal\nbe\nAl\npl\nal\n',
                    'sortbg': b'Alpha:3\nalpha:10\nalpha:10\nbeta:2\nbeta:2\nplain\n',
                    'sortpipe': b'row2999\n', 'uniqpipe': b'row0000\n', 'cutpipe': b'0000\n',
                    'trpipe': b'ROW0000\n', 'binbytes': b'12239\n', 'trbg': b'BACKGROUND\n', 'reuse': b'ok\n'}
        for name, content in expected.items():
            assert read(name) == content, (name, read(name), content)
        for name in ('sorterr', 'uniqerr', 'cuterr', 'uniqout'):
            assert b'cannot open' in read(name), name
        assert read('private') == b'secret\n'
        assert read('trbin') == bytes(i % 251 for i in range(12 * 1024)).replace(b'\0', b'X')
        return 'Normal-user commands; exact file bytes, four stages, permissions, binary streams, early exits and repeated jobs.'

    report.check('text-tools', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
