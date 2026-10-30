#!/usr/bin/env python3
"""TCP active exchanges through a deterministic lossy peer and real host TCP."""
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
from dhcp_peer import wait_serial

MASK32 = 0xffffffff


class TcpRouter(DnsRouter):
    def __init__(self):
        self.streams = {}
        self.completed = 0
        self.losses = set()
        super().__init__()

    def reply_tcp(self, frame, seq, ack, flags, data=b''):
        local, port = struct.unpack('!HH', frame[34:38])
        options = bytes.fromhex('02040200') if flags & 2 else b''
        tcp = bytearray(struct.pack('!HHIIBBHHH', port, local, seq & MASK32, ack & MASK32,
                                   (5 + len(options) // 4) << 4, flags, 2048, 0, 0) + options + data)
        source = frame[30:34]
        struct.pack_into('!H', tcp, 16, checksum(source + LOCAL + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp))
        ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), 0, 0x4000, 64, 6, 0, source, LOCAL))
        struct.pack_into('!H', ip, 10, checksum(ip))
        self.send((GUEST_MAC + frame[:6] + b'\x08\0' + ip + tcp).ljust(60, b'\0'))

    def handle(self, frame):
        if frame[12:14] != b'\x08\0' or frame[23] != 6:
            return super().handle(frame)
        target = frame[30:34]
        assert target in (GATEWAY, NEIGHBOR, REMOTE)
        assert frame[:6] == (NEIGHBOR_MAC if target == NEIGHBOR else GATEWAY_MAC)
        total = struct.unpack('!H', frame[16:18])[0]
        tcp = frame[34:14 + total]
        assert checksum(frame[14:34]) == 0
        assert checksum(LOCAL + target + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp) == 0
        local, port, seq, ack, offset, flags, window = struct.unpack('!HHIIBBH', tcp[:16])
        data = tcp[(offset >> 4) * 4:]
        assert 49152 <= local <= 65535
        if port == 9999 or flags & 4:
            return
        if port == 7780:
            self.reply_tcp(frame, 0, seq + 1, 20)
            return
        assert port in (7778, 7779)
        key = (target, local)
        if flags & 2:
            assert data == b'' and tcp[20:24] == bytes.fromhex('02040200')
            if key not in self.streams:
                self.streams[key] = dict(start=seq, expected=(seq + 1) & MASK32, data=b'', server=0xfffffffd)
            if port == 7779 and (key, 'syn') not in self.losses:
                self.losses.add((key, 'syn'))
                return
            self.reply_tcp(frame, 0xfffffffd, seq + 1, 18)
            return
        state = self.streams[key]
        if data:
            if port == 7779 and (key, 'data') not in self.losses:
                self.losses.add((key, 'data'))
                return
            if seq == state['expected']:
                state['data'] += data
                state['expected'] = (seq + len(data)) & MASK32
            self.reply_tcp(frame, 0xfffffffe, state['expected'], 16)
            return
        if flags & 1:
            if port == 7779 and (key, 'fin') not in self.losses:
                self.losses.add((key, 'fin'))
                return
            assert seq == state['expected'] or seq == (state['expected'] - 1) & MASK32
            if not state.get('closed'):
                state['expected'] = (seq + 1) & MASK32
                state['closed'] = True
            reply = state['data']
            # Wrong reset sequence and impossible ACK must not complete the exchange.
            self.reply_tcp(frame, 88, state['expected'], 20)
            self.reply_tcp(frame, 0xfffffffe, state['expected'] + 100, 16)
            if len(reply) > 2:
                self.reply_tcp(frame, 1, state['expected'], 24, reply[3:])  # out of order
                self.reply_tcp(frame, 0xfffffffe, state['expected'], 24, reply[:3])
            # Overlapping retransmission tests trimming across 32-bit sequence wrap.
            self.reply_tcp(frame, 0xfffffffe, state['expected'], 25, reply)
            state['end'] = (0xfffffffe + len(reply) + 1) & MASK32
            return
        if state.get('end') == ack and not state.get('done'):
            state['done'] = True
            self.completed += 1


class HostEcho:
    def __init__(self):
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen(8)
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.received = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                client, _ = self.socket.accept()
            except socket.timeout:
                continue
            with client:
                client.settimeout(10)
                data = b''
                while len(data) <= 512:
                    part = client.recv(513 - len(data))
                    if not part:
                        break
                    data += part
                self.received.append(data)
                client.sendall(data[:3])
                client.sendall(data[3:])

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.socket.close()


def test(firmware, image, timeout, host_echo):
    directory = OUT / ('tcp-' + firmware + ('-host' if host_echo else ''))
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    if not host_echo:
        probe = build_probe(directory, image.parent / 'source', extra=('tcp_probe.cpp', 'TCPPROBE'))
        require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/TCPPROBE']), 'install TCP probe')
        provision_imports(disposable, firmware)
    router = HostEcho() if host_echo else TcpRouter()
    try:
        backend = None if host_echo else f'socket,id=net0,connect=127.0.0.1:{router.port}'
        with boot(directory, disposable, firmware, 'tcp', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'tcp.pcap', network_backend=backend) as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            if host_echo:
                guest.shell(f'tcp 10.0.2.2 {router.port} "hello over TCP"', 'Reply: hello over TCP')
                guest.screen('tcp-host')
                assert router.received == [b'hello over TCP']
            else:
                guest.shell('tcpprobe', 'TCP_ABI_OK')
                guest.shell('runas guest tcpprobe', 'TCP_ABI_OK')
                guest.shell('tcp 10.0.2.2 7779 retransmit', 'Reply: retransmit')
                assert len(router.losses) == 3
                guest.shell('tcp 10.0.2.99 7778 direct', 'Reply: direct')
                guest.shell('runas guest tcp demo.test 7778 routed', 'Reply: routed')
                guest.shell('tcp 10.0.2.2 7778 ""', 'received 0 bytes')
                guest.screen('tcp-demo')
                guest.shell('tcp 10.0.2.2 7780 refused', 'connection refused or reset')
                guest.shell('tcp 10.0.2.2 7778 redirected > /tmp/tcp.txt', '$ ')
                guest.shell('cat /tmp/tcp.txt', 'Reply: redirected')
                guest.qmp.call('set_link', {'name': 'net0', 'up': False})
                guest.shell('tcp 10.0.2.2 7778 offline', 'network unavailable')
                offset = len(guest.serial.read_text(errors='replace'))
                guest.qmp.call('set_link', {'name': 'net0', 'up': True})
                wait_serial(guest, 'NET: DHCP lease bound', offset)
                guest.shell('tcp 10.0.2.2 7778 restored', 'Reply: restored')
        if not host_echo:
            assert router.failure is None, repr(router.failure)
            assert router.completed >= 8
            with boot(directory, disposable, firmware, 'no-nic', timeout, qmp_stdio=True) as guest:
                guest.login('root', ADMIN_PASSWORD)
                guest.shell('tcp 10.0.2.2 7778 offline', 'network unavailable')
        print(f'PASS {firmware}: TCP ' + ('real host echo through QEMU user networking' if host_echo else
              'ABI, ownership, cleanup, SYN/data/FIN retransmission, sequence wrap, overlap, bad ACK/RST, DNS/routing and link recovery'))
    finally:
        router.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--host-echo', action='store_true')
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout, args.host_echo)
