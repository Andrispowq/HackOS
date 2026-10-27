#!/usr/bin/env python3
"""echo, RTC formatting, root-only hardware snapshots and redirected reports."""
import argparse
import re
import shutil
import sys
from common import OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-utilities-' + firmware)
    report = Report('qemu-utilities-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('utilities_probe.cpp', 'UTILPROBE'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/UTILPROBE']), 'install utilities probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'utilities', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('utilprobe', 'UTILITIES_ABI_OK')
            g.shell('runas guest utilprobe', 'UTILITIES_DENIED_OK')
            g.shell('echo tick time date', '\ntick time date\n')
            g.shell('echo -n "two  words" "" end | cat > /tmp/echo', '$ ')
            g.shell('echo > /tmp/empty', '$ ')
            g.shell('echo -- -n > /tmp/literal', '$ ')
            g.shell('echo "100%" >> /tmp/echo', '$ ')
            g.shell('print', 'Unknown command: print')
            g.shell('time --help', 'Usage: time')
            g.shell('time --bogus', 'Usage: time')
            g.shell('time > /tmp/clock', '$ ')
            g.shell('time --date > /tmp/date', '$ ')
            g.shell('time --time > /tmp/time', '$ ')
            g.shell('time --ticks > /tmp/ticks', '$ ')
            g.shell('time -d -t > /tmp/datetime', '$ ')
            g.shell('time -k -a > /tmp/all', '$ ')
            g.shell('runas guest time -d', '$ ')
            g.shell('sysinfo --help', 'Usage: sysinfo')
            g.shell('sysinfo nonsense', 'Usage: sysinfo')
            g.shell('sysinfo cpu extra', 'Usage: sysinfo')
            for section, expected in [('system', 'Boot firmware'), ('cpu', 'Scheduling CPUs'), ('memory', 'Free frames'),
                                      ('memory-map', 'Firmware memory map'), ('disk', '512-byte sectors'),
                                      ('pci', 'PCI functions listed'), ('usb', 'XHCI (driver not implemented)'),
                                      ('ps2', 'PS/2 input'), ('ps/2', 'PS/2 input'), ('pc/2', 'PS/2 input'),
                                      ('display', 'Framebuffer'), ('acpi', 'MCFG')]:
                g.shell('sysinfo ' + section, expected)
            g.shell('sysinfo memory > /tmp/memory', '$ ')
            g.shell('sysinfo pci | cat > /tmp/pci', '$ ')
            g.shell('sysinfo full | cat > /tmp/hardware', '$ ')
            g.shell('runas guest sysinfo memory > /tmp/noinfo 2> /tmp/denied', '$ ')
            g.shell('clear', '$ ')
            g.shell('sysinfo cpu', 'Scheduling CPUs')
            g.shell('sysinfo memory', 'Free frames')
            g.screen('cpu-memory')
            g.shell('clear', '$ ')
            g.shell('sysinfo pci', 'PCI functions listed')
            g.shell('sysinfo usb', 'USB controllers listed')
            g.screen('pci-usb')
            g.shell('clear', '$ ')
            g.shell('time', ' ticks\n')
            g.shell('sysinfo ps2', 'PS/2 input')
            g.shell('sysinfo display', 'Framebuffer')
            g.shell('sysinfo acpi', 'MCFG')
            g.screen('clock-input-acpi')

        def read(name):
            return require_success(command(['mtype', '-i', filesystem(image), '::/TMP/' + name]), name)

        assert read('echo') == 'two  words  end100%\n'
        assert read('empty') == '\n'
        assert read('literal') == '-- -n\n'
        assert re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \| \d+ ticks\n', read('clock'))
        assert re.fullmatch(r'\d{4}-\d{2}-\d{2}\n', read('date'))
        assert re.fullmatch(r'\d{2}:\d{2}:\d{2}\n', read('time'))
        assert re.fullmatch(r'\d+\n', read('ticks'))
        assert re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\n', read('datetime'))
        assert re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \| \d+ ticks\n', read('all'))
        memory = read('memory')
        assert 'Free frames' in memory and 'PCI devices' not in memory and 'CPU\n' not in memory
        assert re.search(r'0000:00:[0-9a-f]{2}\.[0-7]  [0-9a-f]{4}:[0-9a-f]{4}', read('pci'))
        hardware = read('hardware')
        for heading in ('HackOS system report', 'CPU', 'Memory', 'Firmware memory map', 'Disk / block devices',
                        'PCI devices', 'USB controllers', 'PS/2 input', 'Display', 'ACPI tables'):
            assert '\n' + heading + '\n' in '\n' + hardware, heading
        assert ('UEFI' if firmware == 'uefi' else 'BIOS') in hardware
        assert '\x1b' not in hardware
        assert read('noinfo') == ''
        assert read('denied') == 'sysinfo: root privileges required.\n'
        (directory / 'hardware.txt').write_text(hardware)
        return 'Checked RTC fields, echo bytes, every hardware section, direct syscall authorization/copies, pipes and saved output.'

    report.check('utilities', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))
