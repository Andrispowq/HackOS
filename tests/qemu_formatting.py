#!/usr/bin/env python3
"""Check padded console tables and printf in real BIOS/UEFI guests."""
import argparse
import re
import shutil
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-formatting-' + firmware)
    report = Report('qemu-formatting-' + firmware, directory)
    built = OUT / ('build-' + firmware)

    def run():
        image = directory / 'HackOS.img'
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('usability_probe.cpp', 'FMTPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/FMTPROBE']), 'install runtime probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'formatting', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('fmtprobe', 'USABILITY_RUNTIME_OK 17')
            g.shell('groupadd development', 'Group created')
            g.shell('useradd longusername development', 'User created')
            for name, utility in [('users', 'users'), ('groups', 'groups'), ('ps', 'ps'), ('ls', 'ls -l /usr/bin'),
                                  ('memory', 'sysinfo full'), ('pci', 'sysinfo pci'), ('acpi', 'sysinfo acpi')]:
                g.shell(utility + ' > /tmp/' + name + '.txt', '$ ')
            g.shell('clear', '$ ')
            g.shell('sysinfo pci', 'PCI functions listed')
            g.screen('aligned-console-pci')
        tables = {}
        for name in ['users', 'groups', 'ps', 'ls', 'memory', 'pci', 'acpi']:
            tables[name] = require_success(command(['mtype', '-i', filesystem(image), '::/TMP/' + name + '.TXT']), 'read ' + name)
            (directory / (name + '.txt')).write_text(tables[name])
        user_lines = tables['users'].splitlines()
        assert user_lines[0] == f'{"USER":31} {"UID":>5} {"GID":>5}'
        for line in user_lines[1:]:
            assert re.fullmatch(r'\S+ *', line[:31]) and line[31] == ' ' and line[37] == ' '
            assert line[32:37].strip().isdigit() and line[38:43].strip().isdigit()
        assert any(line.startswith('longusername') for line in user_lines)
        for line in tables['groups'].splitlines()[1:]:
            assert line[31] == ' ' and line[32:37].strip().isdigit()
        ps = tables['ps'].splitlines()
        assert ps[0] == f'{"PID":>6} {"PPID":>6} {"PGID":>6} {"UID":>10} {"GID":>10} {"STATE":8} NAME'
        assert any(re.fullmatch(r' *\d+ +\d+ +\d+ +0 +0 +ALIVE +~/USR/BIN/PS', line) for line in ps[1:])
        for line in ps[1:]:
            assert line[21:31].strip().isdigit() and line[32:42].strip().isdigit()
            assert line[43:51].strip() in ['ALIVE', 'WAITING', 'INPUT', 'STOPPED', 'IO', 'STARTING', 'SLEEP', 'ZOMBIE']
            assert line[51] == ' '
        ls = tables['ls'].splitlines()
        assert ls[0] == 'MODE         UID   GID ATTR      BYTES NAME'
        for line in ls[1:]:
            assert line[10] == line[16] == line[22] == line[27] == line[38] == ' ', repr(line)
            assert line[11:16].strip().isdigit() and line[17:22].strip().isdigit() and line[28:38].strip().isdigit()
        pci = [line for line in tables['pci'].splitlines() if re.match(r'  [0-9a-f]{4}:', line)]
        assert pci and all(re.match(r'  [0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.\d  [0-9a-f]{4}:[0-9a-f]{4}    [0-9a-f]{2}:[0-9a-f]{2}:[0-9a-f]{2}     [0-9a-f]{2}   \S', line) for line in pci)
        assert re.search(r'  [0-9a-f]{16}   [0-9a-f]{16}   [0-9a-f]{4}  (yes|no)', tables['memory'])
        assert re.search(r'  [A-Z0-9]+ +\d+ +\d+  \S+ +\S+ +\d+', tables['acpi'])
        return 'Guest printf padding/precision/bounds; aligned users/groups/ps/ls/memory/PCI/ACPI; redirected output and screenshot.'

    report.check('console_tables_and_formatter', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    raise SystemExit(test(args.firmware, args.timeout))
