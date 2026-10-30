#!/usr/bin/env python3
"""Process-owned UDP endpoints: real guest ABI, datagram queues and a host echo demo."""
import argparse
from pathlib import Path
import shutil
import socket
import struct
import threading
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from qemu_dns import DnsRouter
from qemu_routing import LOCAL, GUEST_MAC, GATEWAY, GATEWAY_MAC, NEIGHBOR, NEIGHBOR_MAC, REMOTE, checksum
from session import provision_imports


class UdpRouter(DnsRouter):
    def reply_udp(self, frame, data, source=None, port=None):
        source = frame[30:34] if source is None else source
        incoming = frame[34:42]
        peer, destination = struct.unpack('!HH', incoming[:4])
        port = destination if port is None else port
        udp = bytearray(struct.pack('!HHHH', port, peer, 8 + len(data), 0) + data)
        struct.pack_into('!H', udp, 6, checksum(source + LOCAL + b'\0\x11' + struct.pack('!H', len(udp)) + udp) or 65535)
        ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), 0, 0x4000, 64, 17, 0, source, LOCAL))
        struct.pack_into('!H', ip, 10, checksum(ip))
        self.send((GUEST_MAC + frame[:6] + b'\x08\0' + ip + udp).ljust(60, b'\0'))

    def handle(self, frame):
        if frame[12:14] != b'\x08\0' or frame[23] != 17 or frame[36:38] == b'\0\x35':
            return super().handle(frame)
        target = frame[30:34]
        assert target in (GATEWAY, NEIGHBOR, REMOTE)
        assert frame[:6] == (NEIGHBOR_MAC if target == NEIGHBOR else GATEWAY_MAC)
        assert checksum(frame[14:34]) == 0 and frame[26:30] == LOCAL
        total = struct.unpack('!H', frame[16:18])[0]
        udp = frame[34:14 + total]
        source, destination, length, checked = struct.unpack('!HHHH', udp[:8])
        assert 1024 <= source < 49152 and length == len(udp) and checked
        assert checksum(LOCAL + target + b'\0\x11' + struct.pack('!H', length) + udp) == 0
        if destination == 9999:
            return
        assert destination == 7777
        data = udp[8:]
        self.reply_udp(frame, b'wrong peer', source=bytes([203, 0, 113, 77]))
        self.reply_udp(frame, b'wrong port', port=7778)
        if data == b'burst':
            for i in range(7):
                self.reply_udp(frame, bytes([i]))
        else:
            self.reply_udp(frame, data)


class HostEcho:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.received = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                data, peer = self.socket.recvfrom(513)
                self.received.append(data)
                if len(data) <= 512:
                    self.socket.sendto(data, peer)
            except socket.timeout:
                pass

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.socket.close()


def test(firmware, image, timeout, host_echo):
    directory = OUT / ('udp-' + firmware + ('-host' if host_echo else ''))
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    probe = build_probe(directory, image.parent / 'source', extra=('udp_probe.cpp', 'UDPPROBE'))
    require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/UDPPROBE']), 'install UDP probe')
    provision_imports(disposable, firmware)
    router = HostEcho() if host_echo else UdpRouter()
    try:
        backend = None if host_echo else f'socket,id=net0,connect=127.0.0.1:{router.port}'
        with boot(directory, disposable, firmware, 'udp', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'udp.pcap', network_backend=backend) as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            if host_echo:
                guest.shell(f'udp 10.0.2.2 {router.port} "hello from HackOS"', 'Received 17 bytes: hello from HackOS')
                guest.screen('host-echo')
                assert router.received == [b'hello from HackOS']
            else:
                guest.shell('udpprobe', 'UDP_ABI_OK')
                guest.shell('runas guest udpprobe', 'UDP_ABI_OK')
                guest.shell('clear', '$ ')
                guest.shell('udp 10.0.2.2 7777 "hello from HackOS"', 'Received 17 bytes: hello from HackOS')
                guest.shell('udp 10.0.2.99 7777 direct', 'Received 6 bytes: direct')
                guest.shell('runas guest udp demo.test 7777 routed', 'Received 6 bytes: routed')
                guest.screen('udp-demo')
                guest.shell('udp 10.0.2.2 7777 ""', 'Received 0 bytes:')
                guest.shell('udp 10.0.2.2 9999 silent', 'receive timed out')
                guest.shell('udp 10.0.2.200 7777 absent', 'ARP resolution failed')
                guest.shell('udp 10.0.2.2 0 invalid', 'Usage: udp')
                guest.shell('udp 10.0.2.2 65536 invalid', 'Usage: udp')
                guest.shell('udp 127.0.0.1 7777 invalid', 'unsupported destination')
                guest.shell('udp 10.0.2.2 7777 redirected > /tmp/udp.txt', '$ ')
                guest.shell('cat /tmp/udp.txt', 'Received 10 bytes: redirected')
                guest.qmp.call('set_link', {'name': 'net0', 'up': False})
                guest.shell('udp 10.0.2.2 7777 offline', 'network unavailable')
                guest.qmp.call('set_link', {'name': 'net0', 'up': True})
                guest.shell('udp 10.0.2.2 7777 restored', 'Received 8 bytes: restored')
        if not host_echo:
            assert router.failure is None, repr(router.failure)
            with boot(directory, disposable, firmware, 'no-nic', timeout, qmp_stdio=True) as guest:
                guest.login('root', ADMIN_PASSWORD)
                guest.shell('udp 10.0.2.2 7777 offline', 'network unavailable')
        print(f'PASS {firmware}: ' + ('UDP echo through QEMU user networking' if host_echo else
              'UDP queues, ABI bounds, zero/max payloads, ownership, exit cleanup, direct/routed/DNS peers and failures'))
    finally:
        router.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--host-echo', action='store_true', help='Exercise QEMU user networking with a real loopback UDP server')
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout, args.host_echo)
