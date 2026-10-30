#!/usr/bin/env python3
"""Loopback-only bounded TCP echo server for the HackOS demo."""
import argparse
import socket


def run(port):
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('127.0.0.1', port))
        server.listen(8)
        print(f'TCP demo: tcp 10.0.2.2 {port} "hello over TCP"', flush=True)
        while True:
            connection, _ = server.accept()
            with connection:
                connection.settimeout(5)
                try:
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
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be between 1024 and 65535')
    run(args.port)
