#!/usr/bin/env python3
"""Loopback-only HTTP demo and deterministic response fixtures; no filesystem serving."""
import argparse
import socketserver

WELCOME = b'Hello from HackOS HTTP!\nFetched using TCP streams and HTTP/1.1.\nTry /chunked, /large or /binary.\n'
LARGE = b'0123456789abcdef' * 512
BINARY = bytes(range(256))


def response(target, host):
    path = target.split('?', 1)[0]
    if path == '/chunked':
        return (b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'
                b'6;demo="yes"\r\nHello \r\n8\r\nchunked!\r\n1\r\n\n\r\n0\r\nX-Demo: complete\r\n\r\n')
    if path == '/close':
        return b'HTTP/1.0 200 OK\r\n\r\nClose-delimited response.\n'
    if path == '/empty':
        return b'HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n'
    if path == '/redirect':
        return b'HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n'
    if path == '/bad':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx'
    if path == '/truncated':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort'
    if path == '/compressed':
        return b'HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 1\r\n\r\nx'
    if path == '/oversize':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 32769\r\n\r\n'
    if path == '/controls':
        body = b'safe\x1b[31m\x00\xff\n'
    elif path == '/large':
        body = LARGE
    elif path == '/binary':
        body = BINARY
    elif path == '/boundary':
        body = b'x' * 32768
    elif path == '/host':
        body = b'Host: ' + host.encode('ascii') + b'\nTarget: ' + target.encode('ascii') + b'\n'
    elif path in ('/', '/info'):
        body = WELCOME
    else:
        return b'HTTP/1.1 404 Not Found\r\nContent-Length: 8\r\n\r\nmissing\n'
    prefix = b'HTTP/1.1 103 Early Hints\r\nLink: </large>\r\n\r\n' if path == '/info' else b''
    return prefix + b'HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        try:
            data = b''
            while b'\r\n\r\n' not in data and len(data) < 2048:
                part = self.request.recv(min(512, 2048 - len(data)))
                if not part:
                    return
                data += part
            if b'\r\n\r\n' not in data:
                return
            lines = data.split(b'\r\n')
            method, target, version = lines[0].decode('ascii').split(' ')
            headers = {}
            for line in lines[1:]:
                if not line:
                    break
                key, value = line.decode('ascii').split(':', 1)
                headers[key.lower()] = value.strip()
            if method != 'GET' or version != 'HTTP/1.1' or not target.startswith('/') or 'host' not in headers:
                return
            if hasattr(self.server, 'requests'):
                self.server.requests.append((target, headers))
            message = response(target, headers['host'])
            # Independent writes exercise arbitrary TCP/HTTP segmentation.
            at = 0
            for size in (1, 2, 7, 31, 127):
                self.request.sendall(message[at:at + size])
                at += size
                if at >= len(message):
                    return
            while at < len(message):
                self.request.sendall(message[at:at + 512])
                at += 512
        except (OSError, ValueError, UnicodeError):
            return


class Server(socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be between 1024 and 65535')
    with Server(('127.0.0.1', args.port), Handler) as server:
        print(f'HTTP demo: http http://10.0.2.2:{args.port}/', flush=True)
        server.serve_forever()
