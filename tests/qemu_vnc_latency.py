#!/usr/bin/env python3
"""Measure keyboard-to-changed-pixels through the Docker/noVNC WebSocket path.

Uses real websockify + QEMU RFB with raw rectangles. Excludes browser decoding,
canvas/compositor paint and host-to-container network transit; no guest debugger.
"""
import argparse
import base64
import hashlib
import json
import os
import shutil
import socket
import struct
import subprocess
import time
from common import OUT
from qemu_accounts import boot, ADMIN_PASSWORD


class WebSocket:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=10)
        self.socket.settimeout(10)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = b''
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall(('GET /websockify HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n'
                             'Connection: Upgrade\r\nSec-WebSocket-Key: ' + key +
                             '\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: binary\r\n\r\n').encode())
        header = b''
        while not header.endswith(b'\r\n\r\n'):
            header += self.raw(1)
            assert len(header) < 8192
        assert b' 101 ' in header, header
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert accept in header, header

    def raw(self, size):
        data = b''
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            assert chunk, 'WebSocket closed'
            data += chunk
        return data

    def send(self, data, opcode=2):
        mask = os.urandom(4)
        size = len(data)
        head = bytes([0x80 | opcode, 0x80 | size]) if size < 126 else bytes([0x80 | opcode, 0xfe]) + struct.pack('!H', size)
        self.socket.sendall(head + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(data)))

    def read(self, size):
        while len(self.buffer) < size:
            first, second = self.raw(2)
            length = second & 127
            if length == 126:
                length = struct.unpack('!H', self.raw(2))[0]
            elif length == 127:
                length = struct.unpack('!Q', self.raw(8))[0]
            assert length <= 32 * 1024 * 1024 and not second & 128
            data = self.raw(length)
            opcode = first & 15
            if opcode == 9:
                self.send(data, 10)
            else:
                assert opcode in (0, 2), 'Unexpected WebSocket opcode ' + str(opcode)
                self.buffer += data
        data, self.buffer = self.buffer[:size], self.buffer[size:]
        return data


class Rfb:
    def __init__(self, connection):
        self.connection = connection
        assert connection.read(12) == b'RFB 003.008\n'
        connection.send(b'RFB 003.008\n')
        count = connection.read(1)[0]
        assert count and 1 in connection.read(count)
        connection.send(b'\x01')
        assert connection.read(4) == bytes(4)
        connection.send(b'\x01')
        init = connection.read(24)
        self.width, self.height = struct.unpack('!HH', init[:4])
        connection.read(struct.unpack('!I', init[20:])[0])
        assert 760 <= self.width <= 4096 and 502 <= self.height <= 2160
        connection.send(bytes(4) + struct.pack('!BBBBHHHBBBxxx', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
        connection.send(struct.pack('!BBHi', 2, 0, 1, 0))
        self.terminal = bytearray(720 * 432 * 4)

    def update(self, incremental):
        c = self.connection
        c.send(struct.pack('!BBHHHH', 3, incremental, 0, 0, self.width, self.height))
        kind = c.read(1)[0]
        assert kind == 0, 'Unexpected RFB message ' + str(kind)
        _, count = struct.unpack('!BH', c.read(3))
        for _ in range(count):
            x, y, width, height, encoding = struct.unpack('!HHHHi', c.read(12))
            assert encoding == 0 and x + width <= self.width and y + height <= self.height
            pixels = c.read(width * height * 4)
            left, right = max(x, 40), min(x + width, 760)
            if left >= right:
                continue
            for row in range(max(y, 70), min(y + height, 502)):
                source = ((row - y) * width + left - x) * 4
                target = ((row - 70) * 720 + left - 40) * 4
                self.terminal[target:target + (right - left) * 4] = pixels[source:source + (right - left) * 4]
        return hashlib.sha256(self.terminal).hexdigest()

    def key(self, code):
        for down in (1, 0):
            self.connection.send(struct.pack('!BBHI', 4, down, 0, code))


def measure(firmware, samples):
    directory = OUT / ('qemu-vnc-latency-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    image = directory / 'HackOS.img'
    shutil.copy2(OUT / ('build-' + firmware) / 'HackOS.img', image)
    with boot(directory, image, firmware, 'vnc', 120, vnc=True) as g:
        g.setup()
        g.enter('root', 'New password: ')
        g.password(ADMIN_PASSWORD, 'Action: ')
        g.enter('login', 'Login: ')
        g.enter('root', 'Password: ')
        g.enter(ADMIN_PASSWORD, 'TERMINAL_READY')
        g.wait('root@root:~$ ')
        with (directory / 'websockify.log').open('w') as log:
            proxy = subprocess.Popen(['websockify', '127.0.0.1:6082', '127.0.0.1:5902'], stdout=log, stderr=log)
            connection = None
            try:
                deadline = time.monotonic() + 10
                while True:
                    try:
                        connection = WebSocket(6082)
                        break
                    except ConnectionRefusedError:
                        assert proxy.poll() is None and time.monotonic() < deadline
                        time.sleep(0.05)
                rfb = Rfb(connection)
                previous = rfb.update(False)
                for attempt in range(30):
                    time.sleep(0.1)
                    current = rfb.update(False)
                    if current == previous and b'\xf5\xee\xe5' in rfb.terminal:
                        break
                    previous = current
                else:
                    raise AssertionError('Initial terminal pixels did not settle')
                results = []
                for _ in range(samples):
                    # Drain old output before timing a fresh key. The clock lies
                    # outside the terminal rectangle and cannot satisfy the check.
                    before = rfb.update(False)
                    started = time.monotonic()
                    rfb.key(ord('a'))
                    while rfb.update(True) == before:
                        assert time.monotonic() - started < 10, 'No terminal pixel update'
                    results.append(time.monotonic() - started)
                    time.sleep(0.2)
                result = {'firmware': firmware, 'encoding': 'raw', 'key_to_terminal_pixels_seconds': results,
                          'scope': 'websockify/RFB + guest render; excludes browser paint and host/container transit'}
                (directory / 'timings.json').write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps(result, indent=2), flush=True)
            finally:
                if connection:
                    connection.socket.close()
                proxy.terminate()
                proxy.wait(timeout=10)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--samples', type=int, default=5)
    args = parser.parse_args()
    if not 1 <= args.samples <= 20:
        parser.error('samples must be 1..20')
    measure(args.firmware, args.samples)
