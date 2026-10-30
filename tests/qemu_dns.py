#!/usr/bin/env python3
"""Bounded DNS/UDP and hostname ping against a deterministic virtual DNS server."""
import argparse
from dhcp_peer import wait_serial
from pathlib import Path
import shutil
import struct
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from qemu_routing import Router, LOCAL, GUEST_MAC, REMOTE, checksum
from session import provision_imports

SERVER = bytes([10, 0, 2, 3])
SERVER_MAC = bytes.fromhex('52550a000203')


def name_wire(name):
    return b''.join(bytes([len(label)]) + label.encode() for label in name.split('.')) + b'\0'


class DnsRouter(Router):
    def __init__(self):
        self.queries = {}
        self.ports = set()
        super().__init__()

    def reply(self, query, payload, source=SERVER, port=None):
        destination_port = struct.unpack('!H', query[34:36])[0] if port is None else port
        udp = bytearray(struct.pack('!HHHH', 53, destination_port, 8 + len(payload), 0) + payload)
        pseudo = source + LOCAL + bytes([0, 17]) + struct.pack('!H', len(udp))
        struct.pack_into('!H', udp, 6, checksum(pseudo + udp) or 65535)
        ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), 0, 0x4000, 64, 17, 0, source, LOCAL))
        struct.pack_into('!H', ip, 10, checksum(ip))
        self.send(GUEST_MAC + SERVER_MAC + b'\x08\0' + ip + udp)

    def handle(self, frame):
        if len(frame) >= 42 and frame[12:14] == b'\x08\0' and frame[23] == 17 and frame[34:38] == b'\0\x44\0\x43':
            return super().handle(frame)
        if frame[12:14] == b'\x08\x06' and frame[20:22] == b'\0\1' and frame[38:42] == SERVER:
            reply = GUEST_MAC + SERVER_MAC + bytes.fromhex('08060001080006040002') + SERVER_MAC + SERVER + GUEST_MAC + LOCAL
            self.send(reply.ljust(60, b'\0'))
            return
        if frame[12:14] != b'\x08\0' or frame[23] != 17:
            return super().handle(frame)
        assert frame[:6] == SERVER_MAC and frame[26:30] == LOCAL and frame[30:34] == SERVER
        assert checksum(frame[14:34]) == 0
        total = struct.unpack('!H', frame[16:18])[0]
        udp = frame[34:14 + total]
        source_port, port, size, checked = struct.unpack('!HHHH', udp[:8])
        assert port == 53 and 49152 <= source_port <= 65535 and checked and size == len(udp)
        assert checksum(LOCAL + SERVER + bytes([0, 17]) + struct.pack('!H', size) + udp) == 0
        self.ports.add(source_port)
        query = udp[8:]
        assert query[2:12] == bytes.fromhex('01000001000000000000')
        at, labels = 12, []
        while query[at]:
            length = query[at]
            labels.append(query[at + 1:at + 1 + length].decode())
            at += 1 + length
        assert query[at:] == b'\0\0\1\0\1'
        name = '.'.join(labels)
        self.queries[name] = self.queries.get(name, 0) + 1
        if name == 'silent.test' or (name == 'retry.test' and self.queries[name] < 3):
            return
        question = query[12:]
        flags, answer = 0x8180, b''
        if name in ('demo.test', 'retry.test'):
            answer = b'\xc0\x0c' + struct.pack('!HHIH', 1, 1, 60, 4) + REMOTE
            count = 1
        elif name == 'alias.test':
            alias = name_wire('demo.test')
            answer = b'\xc0\x0c' + struct.pack('!HHIH', 5, 1, 30, len(alias)) + alias
            answer += alias + struct.pack('!HHIH', 1, 1, 60, 4) + REMOTE
            count = 2
        else:
            count = 0
            flags = {'missing.test': 0x8183, 'error.test': 0x8182, 'large.test': 0x8380,
                     'empty.test': 0x8180}.get(name)
            assert flags is not None, name
        payload = query[:2] + struct.pack('!HHHHH', flags, 1, count, 0, 0) + question + answer
        if name == 'demo.test':
            poison = payload[:-4] + bytes([203, 0, 113, 77])
            wrong_id = bytes([poison[0] ^ 1]) + poison[1:]
            self.reply(frame, wrong_id)
            # Correct DNS contents from a different address/port cannot complete it.
            self.reply(frame, poison, source=bytes([10, 0, 2, 99]))
            self.reply(frame, poison, port=source_port ^ 1)
        self.reply(frame, payload)


def test(firmware, image, timeout, internet):
    directory = OUT / ('dns-' + firmware + ('-internet' if internet else ''))
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    probe = build_probe(directory, image.parent / 'source', extra=('dns_probe.cpp', 'DNSPROBE'))
    require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/DNSPROBE']), 'install DNS probe')
    provision_imports(disposable, firmware)
    router = None if internet else DnsRouter()
    try:
        backend = None if internet else f'socket,id=net0,connect=127.0.0.1:{router.port}'
        with boot(directory, disposable, firmware, 'dns', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'dns.pcap', network_backend=backend) as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            if internet:
                guest.shell('nslookup example.com', 'TTL: ')
                guest.screen('internet-dns')
            else:
                guest.shell('dnsprobe', 'DNS_ABI_OK')
                guest.shell('runas guest dnsprobe', 'DNS_ABI_OK')
                guest.shell('clear', '$ ')
                guest.shell('nslookup DEMO.TEST.', 'Address: 198.51.100.9')
                guest.shell('nslookup alias.test', 'TTL: 30 seconds')
                guest.shell('runas guest ping -c 2 demo.test', '2 probes, 2 received, 0% loss')
                guest.screen('dns-and-ping')
                guest.shell('nslookup retry.test', 'Address: 198.51.100.9')
                for name, expected in [('missing', 'name not found'), ('error', 'server returned an error'),
                                       ('large', 'response truncated'), ('empty', 'no supported IPv4 answer'),
                                       ('silent', 'lookup timed out')]:
                    guest.shell('nslookup ' + name + '.test', expected)
                guest.shell('ping -c 1 missing.test', 'name not found')
                guest.shell('nslookup demo.test > /tmp/dns.txt', '$ ')
                guest.shell('cat /tmp/dns.txt', 'Address: 198.51.100.9')
                for name in ('-bad.test', 'bad..test', '1.2.3.999'):
                    guest.shell('nslookup ' + name, 'invalid hostname')
                guest.qmp.call('set_link', {'name': 'net0', 'up': False})
                guest.shell('nslookup demo.test', 'network unavailable')
                offset = len(guest.serial.read_text(errors="replace"))
                guest.qmp.call('set_link', {'name': 'net0', 'up': True})
                wait_serial(guest, 'NET: DHCP lease bound', offset)
                guest.shell('nslookup demo.test', 'Address: 198.51.100.9')
        if router:
            assert router.failure is None, repr(router.failure)
            assert router.queries['retry.test'] == 3
            assert len(router.ports) >= 8
        print(f'PASS {firmware}: ' + ('public DNS through QEMU user networking' if internet else
              'DNS/UDP, hostname ping, CNAME, retries, errors, validation, ownership, cleanup and link recovery'))
    finally:
        if router:
            router.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--internet', action='store_true', help='Opt-in live example.com lookup through QEMU user networking')
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout, args.internet)
