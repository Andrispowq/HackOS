#!/usr/bin/env python3
"""HTTP GET, framing failures, exact downloads and real host interoperability."""
import argparse
from pathlib import Path
import shutil
import struct
import threading
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_tcp import TcpRouter, MASK32
from qemu_routing import LOCAL, checksum
from http_demo import Handler, Server, response, WELCOME, LARGE, BINARY


class HttpRouter(TcpRouter):
    def __init__(self):
        self.http = {}
        self.requests = []
        super().__init__()

    def handle(self, frame):
        if frame[12:14] != b'\x08\0' or frame[23] != 6:
            return super().handle(frame)
        total = struct.unpack('!H', frame[16:18])[0]
        tcp = frame[34:14 + total]
        local, port, seq, ack, offset, flags, window = struct.unpack('!HHIIBBH', tcp[:16])
        assert port == 8080
        assert checksum(frame[14:34]) == 0
        assert checksum(LOCAL + frame[30:34] + b'\0\x06' + struct.pack('!H', len(tcp)) + tcp) == 0
        data = tcp[(offset >> 4) * 4:]
        key = (frame[30:34], local)
        if flags & 4:
            self.http.pop(key, None)
            return
        if flags & 2:
            self.http.setdefault(key, dict(rx=(seq + 1) & MASK32, tx=0xfffff001, request=b'', message=None, offset=0, fin=False))
            self.reply_tcp(frame, 0xfffff000, seq + 1, 18)
            return
        state = self.http[key]
        if data:
            assert seq == state['rx']
            state['rx'] = (seq + len(data)) & MASK32
            state['request'] += data
            assert len(state['request']) <= 2048
            if b'\r\n\r\n' in state['request']:
                lines = state['request'].decode('ascii').split('\r\n')
                method, target, version = lines[0].split(' ')
                assert method == 'GET' and version == 'HTTP/1.1'
                headers = {}
                for line in lines[1:]:
                    if not line:
                        break
                    name, value = line.split(':', 1)
                    headers[name.lower()] = value.strip()
                assert headers['accept-encoding'] == 'identity' and headers['connection'] == 'close'
                self.requests.append((target, headers))
                state['message'] = response(target, headers['host'])
            else:
                self.reply_tcp(frame, state['tx'], state['rx'], 16)
                return
        if flags & 1:
            state['rx'] = (seq + 1) & MASK32
            self.reply_tcp(frame, state['tx'], state['rx'], 16)
        message = state['message']
        if message is None or ack != state['tx'] or state['fin']:
            return
        size = min(73, window, len(message) - state['offset'])
        if size:
            payload = message[state['offset']:state['offset'] + size]
            self.reply_tcp(frame, state['tx'], state['rx'], 24, payload)
            state['offset'] += size
            state['tx'] = (state['tx'] + size) & MASK32
        elif state['offset'] == len(message):
            self.reply_tcp(frame, state['tx'], state['rx'], 17)
            state['tx'] = (state['tx'] + 1) & MASK32
            state['fin'] = True


class HostHttp:
    def __init__(self):
        self.server = Server(('127.0.0.1', 0), Handler)
        self.server.requests = []
        self.requests = self.server.requests
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.thread.join(2)
        self.server.server_close()


def test(firmware, image, timeout, host):
    directory = OUT / ('http-' + firmware + ('-host' if host else ''))
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    peer = HostHttp() if host else HttpRouter()
    port = peer.port if host else 8080
    url = f'http://10.0.2.2:{port}'
    try:
        backend = None if host else f'socket,id=net0,connect=127.0.0.1:{peer.port}'
        with boot(directory, disposable, firmware, 'http', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'http.pcap', network_backend=backend) as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            guest.shell(f'http {url}/', 'Hello from HackOS HTTP!')
            guest.shell(f'runas guest http {url}/chunked', 'Hello chunked!')
            guest.shell(f'http {url}/close', 'Close-delimited response.')
            guest.shell(f'http {url}/info', 'Hello from HackOS HTTP!')
            guest.shell(f'http -o /tmp/http-large {url}/large', 'Saved 8192 bytes.')
            guest.shell('wc -c /tmp/http-large', '8192')
            guest.shell(f'http -o /tmp/http-binary {url}/binary', 'Saved 256 bytes.')
            guest.shell(f'http {url}/chunked > /tmp/http-chunk', 'Received 15 bytes.')
            guest.shell(f'http {url}/binary | wc -c', '256')
            guest.shell(f'http {url}/controls', r'safe\x1b[31m\x00\xff')
            guest.screen('http-demo')
            guest.shell(f'http {url}/empty', 'Received 0 bytes.')
            guest.shell('echo untouched > /tmp/http-keep', '$ ')
            guest.shell(f'http -o /tmp/http-keep {url}/missing', 'unsuccessful HTTP status')
            guest.shell('cat /tmp/http-keep', 'untouched')
            guest.shell(f'http {url}/redirect', 'redirect not followed')
            guest.shell(f'http {url}/bad', 'duplicate or empty Content-Length')
            guest.shell(f'http {url}/truncated', 'truncated response')
            guest.shell(f'http {url}/compressed', 'compressed responses are unsupported')
            guest.shell(f'http -o /tmp/http-keep {url}/oversize', 'response body too large')
            guest.shell('cat /tmp/http-keep', 'untouched')
            before = len(peer.requests)
            guest.shell('http https://example.com/', 'HTTPS requires TLS')
            assert len(peer.requests) == before
            if not host:
                guest.shell('http "http://demo.test:8080/host?x=1#omit"', 'Target: /host?x=1')
                assert peer.requests[-1] == ('/host?x=1', {'host': 'demo.test:8080', 'user-agent': 'HackOS/0.1',
                       'accept': '*/*', 'accept-encoding': 'identity', 'connection': 'close'})
                assert peer.failure is None, repr(peer.failure)
        for name, expected in [('http-large', LARGE), ('http-binary', BINARY), ('http-chunk', b'Hello chunked!\n'),
                               ('http-keep', b'untouched\n')]:
            local = directory / name
            require_success(command(['mcopy', '-o', '-i', filesystem(disposable), f'::/TMP/{name}', local]), 'read saved response')
            assert local.read_bytes() == expected, name
        print(f'PASS {firmware}: HTTP ' + ('real host' if host else 'virtual DNS/routed peer') +
              ', GET, chunked/length/EOF framing, informational/empty responses, binary/pipeline/files, malformed/truncated/status failures')
    finally:
        peer.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--host', action='store_true')
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout, args.host)
