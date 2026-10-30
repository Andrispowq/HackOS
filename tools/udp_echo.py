#!/usr/bin/env python3
"""Local-only UDP echo peer for the HackOS emulator demo; no Internet service."""
import argparse
import socket


def run(port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(('127.0.0.1', port))
        print(f'UDP demo: guest can use udp 10.0.2.2 {port} "hello from HackOS"', flush=True)
        while True:
            data, peer = server.recvfrom(513)
            if len(data) <= 512:
                server.sendto(data, peer)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=7777)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be between 1024 and 65535')
    run(args.port)
