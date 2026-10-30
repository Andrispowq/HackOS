#!/usr/bin/env python3
"""Loopback-only bounded TCP echo server for the HackOS demo."""
import argparse
import socket


def run(port, stream=False):
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('127.0.0.1', port))
        server.listen(8)
        example = f'tcp --stream 10.0.2.2 {port} 256 "hello over TCP"' if stream else f'tcp 10.0.2.2 {port} "hello over TCP"'
        print(f'TCP demo: {example}', flush=True)
        while True:
            connection, _ = server.accept()
            with connection:
                connection.settimeout(5)
                try:
                    if stream:
                        while True:
                            part = connection.recv(512)
                            if not part:
                                break
                            connection.sendall(part)
                        continue
                    data = b''
                    while len(data) <= 512:
                        part = connection.recv(513 - len(data))
                        if not part:
                            connection.sendall(data)
                            break
                        data += part
                except (TimeoutError, ConnectionError, OSError):
                    pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=7778)
    parser.add_argument('--stream', action='store_true', help='Echo each read immediately, keeping the connection open')
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be between 1024 and 65535')
    run(args.port, args.stream)
