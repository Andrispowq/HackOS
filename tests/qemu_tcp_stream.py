#!/usr/bin/env python3
"""Incremental TCP handles, scheduler poll, backpressure and real stream echo."""
import argparse
from pathlib import Path
import shutil
import socket
import struct
import threading
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from qemu_tcp import TcpRouter, MASK32
from qemu_routing import LOCAL, GATEWAY, NEIGHBOR, REMOTE, checksum
from session import provision_imports
from dhcp_peer import wait_serial


class StreamRouter(TcpRouter):
    def __init__(self):
        self.live = {}
        self.zero_windows = 0
        super().__init__()

    def handle(self, frame):
        if frame[12:14] != b'\x08\0' or frame[23] != 6:
            return super().handle(frame)
        total = struct.unpack('!H', frame[16:18])[0]
        tcp = frame[34:14 + total]
        local, port, seq, ack, offset, flags, window = struct.unpack('!HHIIBBH', tcp[:16])
        if port not in (7781, 7782, 7783):
            return super().handle(frame)
        assert checksum(frame[14:34]) == 0
        assert checksum(LOCAL + frame[30:34] + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp) == 0
        data = tcp[(offset >> 4) * 4:]
        key = (frame[30:34], local)
        if flags & 4:
            self.live.pop(key, None)
            return
        if flags & 2:
            self.live.setdefault(key, dict(rx=(seq + 1) & MASK32, tx=0xfffff001, last=None, offset=0))
            self.reply_tcp(frame, 0xfffff000, seq + 1, 18)
            return
        state = self.live[key]
        if not window:
            self.zero_windows += 1
        if port == 7783:
            if flags & 1:
                self.reply_tcp(frame, state['tx'], seq + 1, 16)
                return
            if ack != state['tx']:
                return
            size = min(512, window, 8192 - state['offset'])
            if size:
                payload = bytes((state['offset'] + i) & 255 for i in range(size))
                self.reply_tcp(frame, state['tx'], state['rx'], 24, payload)
                state['tx'] = (state['tx'] + size) & MASK32
                state['offset'] += size
            elif state['offset'] == 8192 and not state.get('fin'):
                self.reply_tcp(frame, state['tx'], state['rx'], 17)
                state['tx'] = (state['tx'] + 1) & MASK32
                state['fin'] = True
            return
        if data:
            if port == 7782 and not state.get('dropped'):
                state['dropped'] = True
                return
            if seq == state['rx']:
                state['rx'] = (seq + len(data)) & MASK32
                state['last'] = (state['tx'], data)
                self.reply_tcp(frame, state['tx'], state['rx'], 24, data)
                state['tx'] = (state['tx'] + len(data)) & MASK32
            elif state['last']:
                position, payload = state['last']
                self.reply_tcp(frame, position, state['rx'], 24, payload)
        elif flags & 1:
            if 'finseq' not in state:
                state['finseq'] = state['tx']
                state['tx'] = (state['tx'] + 1) & MASK32
                state['rx'] = (seq + 1) & MASK32
            self.reply_tcp(frame, state['finseq'], state['rx'], 17)
        elif seq != state['rx']:
            self.reply_tcp(frame, state['tx'], state['rx'], 16)


class HostStream:
    def __init__(self):
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen(8)
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.connections = 0
        self.total = 0
        self.failure = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    client, _ = self.socket.accept()
                except socket.timeout:
                    continue
                with client:
                    self.connections += 1
                    client.settimeout(20)
                    while True:
                        data = client.recv(73)  # Deliberately split application writes.
                        if not data:
                            break
                        self.total += len(data)
                        client.sendall(data)
        except Exception as error:
            self.failure = error

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.socket.close()


def test(firmware, image, timeout, host_echo):
    directory = OUT / ('tcp-stream-' + firmware + ('-host' if host_echo else ''))
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    if not host_echo:
        probe = build_probe(directory, image.parent / 'source', extra=('tcp_stream_probe.cpp', 'TCPSTREAMPROBE'))
        require_success(command(['mcopy', '-o', '-i', filesystem(disposable), probe, '::/USR/BIN/TCPSTREAMPROBE']), 'install stream probe')
        provision_imports(disposable, firmware)
    peer = HostStream() if host_echo else StreamRouter()
    try:
        backend = None if host_echo else f'socket,id=net0,connect=127.0.0.1:{peer.port}'
        with boot(directory, disposable, firmware, 'stream', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'stream.pcap', network_backend=backend) as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            if host_echo:
                guest.shell(f'tcp --stream 10.0.2.2 {peer.port} 32 "' + 'x' * 100 + '"',
                            'TCP stream complete: 32 chunks, sent 3200 bytes, received 3200 bytes on one connection')
                assert peer.connections == 1 and peer.total == 3200 and peer.failure is None
                guest.screen('real-stream')
            else:
                guest.shell('tcpstreamprobe', 'TCP_STREAM_OK')
                guest.shell('runas guest tcpstreamprobe', 'TCP_STREAM_OK')
                guest.shell('tcp --stream demo.test 7781 256 "hello over TCP"',
                            'TCP stream complete: 256 chunks, sent 3584 bytes, received 3584 bytes on one connection')
                guest.shell('tcp --stream 10.0.2.99 7782 4 lost',
                            'TCP stream complete: 4 chunks, sent 16 bytes, received 16 bytes on one connection')
                guest.screen('stream-demo')
                assert peer.zero_windows >= 2
                guest.qmp.call('set_link', {'name': 'net0', 'up': False})
                guest.shell('tcp --stream 10.0.2.2 7781 1 offline', 'stream connection failed')
                offset = len(guest.serial.read_text(errors='replace'))
                guest.qmp.call('set_link', {'name': 'net0', 'up': True})
                wait_serial(guest, 'NET: DHCP lease bound', offset)
                guest.shell('tcp --stream 10.0.2.2 7781 2 restored', 'received 16 bytes on one connection')
                assert peer.failure is None, repr(peer.failure)
        print(f'PASS {firmware}: TCP streams ' + ('real host, segmented replies and 3200-byte transfer' if host_echo else
              'ABI, ownership/exit, poll, nondestructive failed copy, 8 KiB receive, backpressure, half-close, DNS/routing and loss/recovery'))
    finally:
        peer.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--host-echo', action='store_true')
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout, args.host_echo)
