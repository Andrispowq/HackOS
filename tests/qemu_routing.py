#!/usr/bin/env python3
"""Deterministic on-link and routed ICMP against a loopback-only virtual peer."""
import argparse
from pathlib import Path
import shutil
import socket
import struct
import threading
from common import OUT
from qemu_accounts import boot, ADMIN_PASSWORD

LOCAL = bytes([10, 0, 2, 15])
GATEWAY = bytes([10, 0, 2, 2])
NEIGHBOR = bytes([10, 0, 2, 99])
REMOTE = bytes([198, 51, 100, 9])
SILENT = bytes([198, 51, 100, 10])
UNREACHABLE = bytes([203, 0, 113, 9])
EXCEEDED = bytes([203, 0, 113, 10])
GUEST_MAC = bytes.fromhex('525400123456')
GATEWAY_MAC = bytes.fromhex('52550a000202')
NEIGHBOR_MAC = bytes.fromhex('52550a000263')


def checksum(data):
    data += b'\0' if len(data) % 2 else b''
    value = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def ipv4(source, destination, data):
    ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(data), 0, 0x4000, 64, 1, 0, source, destination))
    struct.pack_into('!H', ip, 10, checksum(ip))
    return bytes(ip) + data


class Router:
    def __init__(self):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.listener.settimeout(0.5)
        self.port = self.listener.getsockname()[1]
        self.connection = None
        self.stopping = threading.Event()
        self.failure = None
        self.arp = []
        self.echoes = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send(self, frame):
        self.connection.sendall(struct.pack('!I', len(frame)) + frame)

    def handle(self, frame):
        assert len(frame) >= 14
        if frame[12:14] == b'\x08\x06':
            assert len(frame) >= 42
            if frame[20:22] != b'\0\1':
                return
            target = frame[38:42]
            self.arp.append(target)
            if target not in (GATEWAY, NEIGHBOR):
                return
            hardware = GATEWAY_MAC if target == GATEWAY else NEIGHBOR_MAC
            reply = GUEST_MAC + hardware + bytes.fromhex('08060001080006040002') + hardware + target + GUEST_MAC + LOCAL
            self.send(reply.ljust(60, b'\0'))
            return
        if frame[12:14] != b'\x08\0':
            return
        assert len(frame) >= 74 and frame[14] == 0x45 and frame[23] == 1
        assert int.from_bytes(frame[16:18], 'big') == 60 and checksum(frame[14:34]) == 0
        assert frame[26:30] == LOCAL and frame[34:36] == b'\x08\0' and checksum(frame[34:74]) == 0
        destination = frame[30:34]
        hardware = NEIGHBOR_MAC if destination == NEIGHBOR else GATEWAY_MAC
        assert frame[:6] == hardware, 'Wrong Ethernet next hop'
        self.echoes.append(destination)
        if destination == SILENT:
            return
        if destination in (UNREACHABLE, EXCEEDED):
            kind, code = (3, 1) if destination == UNREACHABLE else (11, 0)
            quote = frame[14:42]
            # A forged quote with valid checksums must not complete the request.
            bad = bytearray(quote)
            bad[-1] ^= 1
            payload = bytearray(bytes([kind, 7 if kind == 3 else 1, 0, 0, 0, 0, 0, 0]) + bad)
            struct.pack_into('!H', payload, 2, checksum(payload))
            self.send(GUEST_MAC + hardware + b'\x08\0' + ipv4(GATEWAY, LOCAL, payload))
            payload = bytearray(bytes([kind, code, 0, 0, 0, 0, 0, 0]) + quote)
            struct.pack_into('!H', payload, 2, checksum(payload))
            self.send(GUEST_MAC + hardware + b'\x08\0' + ipv4(GATEWAY, LOCAL, payload))
            return
        assert destination in (GATEWAY, NEIGHBOR, REMOTE), 'Unexpected external destination'
        payload = bytearray(frame[34:74])
        payload[0] = 0
        payload[2:4] = b'\0\0'
        struct.pack_into('!H', payload, 2, checksum(payload))
        self.send(GUEST_MAC + hardware + b'\x08\0' + ipv4(destination, LOCAL, payload))

    def run(self):
        try:
            while not self.stopping.is_set():
                try:
                    self.connection, _ = self.listener.accept()
                    break
                except socket.timeout:
                    pass
            if not self.connection:
                return
            self.connection.settimeout(0.5)
            pending = b''
            while not self.stopping.is_set():
                try:
                    data = self.connection.recv(65536)
                    if not data:
                        return
                    pending += data
                except socket.timeout:
                    continue
                while len(pending) >= 4:
                    size = struct.unpack('!I', pending[:4])[0]
                    assert size <= 65536
                    if len(pending) < size + 4:
                        break
                    frame, pending = pending[4:4 + size], pending[4 + size:]
                    self.handle(frame)
        except Exception as error:
            if not self.stopping.is_set():
                self.failure = error

    def close(self):
        self.stopping.set()
        self.listener.close()
        if self.connection:
            self.connection.close()
        self.thread.join(2)


def test(firmware, image, timeout):
    directory = OUT / ('routing-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    router = Router()
    try:
        with boot(directory, disposable, firmware, 'routing', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'routing.pcap', network_backend=f'socket,id=net0,connect=127.0.0.1:{router.port}') as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            for value in ('10.0.2.99', '198.51.100.9'):
                guest.shell('ping -c 2 ' + value, '2 probes, 2 received, 0% loss')
            guest.screen('direct-and-routed')
            guest.shell('runas guest ping -c 1 198.51.100.9', '1 probes, 1 received, 0% loss')
            guest.shell('ping -c 1 203.0.113.9', 'Destination unreachable: seq=1 ICMP code=1')
            guest.shell('ping -c 1 203.0.113.10', 'Time exceeded: seq=1 ICMP code=0')
            guest.shell('ping -c 1 198.51.100.10', 'Request timeout: seq=1')
            guest.shell('ping -c 1 10.0.2.200', 'Next-hop ARP resolution failed: seq=1')
            for value in ('127.0.0.1', '224.0.0.1', '10.0.2.255', '10.0.2.0', '10.0.2.15'):
                guest.shell('ping -c 1 ' + value, 'unsupported address')
            for value in ('1.2.3', '01.2.3.4', '1.2.3.256', 'example.com'):
                guest.shell('ping -c 1 ' + value, 'Usage: ping')
            guest.shell('sysinfo network', '255.255.255.0')
            # Link transitions invalidate neighbor MACs. A fresh request must resolve again.
            before = len(router.arp)
            guest.qmp.call('set_link', {'name': 'net0', 'up': False})
            guest.shell('ping -c 1 10.0.2.99', 'network unavailable')
            guest.qmp.call('set_link', {'name': 'net0', 'up': True})
            guest.shell('ping -c 1 10.0.2.99', '1 probes, 1 received, 0% loss')
            assert NEIGHBOR in router.arp[before:]
        assert router.failure is None, repr(router.failure)
        assert NEIGHBOR in router.arp and GATEWAY in router.arp
        assert REMOTE not in router.arp and UNREACHABLE not in router.arp, 'ARP sent for an off-link destination'
        assert REMOTE in router.echoes and NEIGHBOR in router.echoes
        print(f'PASS {firmware}: direct/routed ping, ICMP errors, silence, ARP failure, validation, normal user and link recovery')
    finally:
        router.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout)
