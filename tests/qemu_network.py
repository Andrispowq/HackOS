#!/usr/bin/env python3
"""Verify E1000 ARP on the wire and graceful boot without a supported NIC."""
import argparse
from pathlib import Path
import shutil
import socket
import select
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

if not __debug__:
    raise SystemExit('Run without Python -O; packet assertions must remain enabled.')


def packets(path):
    data = path.read_bytes()
    assert data[:4] == b'\xd4\xc3\xb2\xa1', 'Expected little-endian pcap'
    assert struct.unpack_from('<I', data, 20)[0] == 1, 'Expected Ethernet capture'
    offset = 24
    result = []
    while offset < len(data):
        assert offset + 16 <= len(data), 'Truncated pcap header'
        size = struct.unpack_from('<I', data, offset + 8)[0]
        offset += 16
        assert offset + size <= len(data), 'Truncated pcap packet'
        result.append(data[offset:offset + size])
        offset += size
    return result


def read_frame(connection):
    def exact(size):
        result = b''
        while len(result) < size:
            part = connection.recv(size - len(result))
            assert part, 'QEMU disconnected'
            result += part
        return result
    size = struct.unpack('!I', exact(4))[0]
    assert size <= 65536, 'Invalid socket frame size'
    return exact(size)


def exercise_peer(connection, serial):
    guest = bytes.fromhex('525400123456')
    peer = bytes.fromhex('525500000002')
    local = bytes([10, 0, 2, 15])
    gateway = bytes([10, 0, 2, 2])
    def frame(op, source_ip=gateway, target_ip=local):
        return (guest + peer + bytes.fromhex('0806000108000604') + struct.pack('!H', op) +
                peer + source_ip + guest + target_ip).ljust(60, b'\0')
    def send(data):
        connection.sendall(struct.pack('!I', len(data)) + data)
    request = read_frame(connection)
    while request[12:14] != b'\x08\x06':
        request = read_frame(connection)
    assert request[12:22] == bytes.fromhex('08060001080006040001')
    # Keep a small window outstanding so slow TCG idle polling does not turn
    # this into forty separate boot-scale waits. Retry dropped frames, while
    # requiring distinct replies across more than two complete ring turns.
    pending = set()
    completed = set()
    next_index = 0
    injected = False
    deadline = time.monotonic() + 180
    last_retry = 0
    while len(completed) < 40 and time.monotonic() < deadline:
        while len(pending) < 4 and next_index < 40:
            pending.add(next_index)
            send(frame(1, source_ip=bytes([10, 0, 2, 100 + next_index])))
            next_index += 1
        if time.monotonic() - last_retry >= 2:
            for index in pending:
                send(frame(1, source_ip=bytes([10, 0, 2, 100 + index])))
            last_retry = time.monotonic()
        if not select.select([connection], [], [], 0.5)[0]:
            continue
        reply = read_frame(connection)
        if len(reply) < 42 or reply[20:22] != b'\0\2':
            continue
        index = reply[41] - 100
        if index not in pending:
            continue
        assert reply[:12] == peer + guest and reply[28:32] == local
        assert reply[32:38] == peer and reply[38:42] == bytes([10, 0, 2, 100 + index])
        pending.remove(index)
        completed.add(index)
        if not injected:
            # RX is active. Subsequent requests provide a processing barrier
            # before checking that malformed/unrelated replies were rejected.
            send(frame(2)[:30])
            send(frame(2, source_ip=bytes([10, 0, 2, 99])))
            send(frame(2, target_ip=bytes([10, 0, 2, 99])))
            broken = bytearray(frame(2))
            broken[6] ^= 2
            send(broken)
            injected = True
    assert len(completed) == 40, f'Only {len(completed)}/40 ARP exchanges completed'
    assert 'NET: ARP gateway 10.0.2.2 resolved ' not in serial.read_text()
    # Bounded rings may drop the final response behind retried requests.
    # Repeat the valid response until the guest confirms it, draining duplicates.
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        send(frame(2))
        end = time.monotonic() + 1
        while time.monotonic() < end and select.select([connection], [], [], 0.1)[0]:
            read_frame(connection)
        if 'NET: ARP gateway 10.0.2.2 resolved ' in serial.read_text():
            return
        time.sleep(0.2)
    raise AssertionError('Guest did not accept the valid gateway reply')


def run(firmware, image, mode, output, timeout):
    output.mkdir(parents=True, exist_ok=True)
    serial = output / f'{mode}.serial.log'
    capture = output / f'{mode}.pcap'
    serial.unlink(missing_ok=True)
    capture.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='hackos-network-') as temporary:
        args = ['qemu-system-x86_64', '-machine', 'q35', '-accel', 'tcg',
                '-cpu', 'qemu64,+rdrand', '-m', '1G', '-smp', 'cores=4,threads=1,sockets=1',
                '-display', 'none', '-monitor', 'none', '-serial', f'file:{serial}',
                '-no-reboot', '-snapshot', '-drive', f'file={image.resolve()},format=raw',
                '-usb', '-device', 'usb-ehci,id=ehci', '-device', 'qemu-xhci,id=xhci']
        listener = None
        connection = None
        if mode in ('peer', 'silent'):
            listener = socket.socket()
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(timeout)
            port = listener.getsockname()[1]
            args += ['-netdev', f'socket,id=net0,connect=127.0.0.1:{port}',
                     '-device', 'e1000,netdev=net0,mac=52:54:00:12:34:56',
                     '-object', f'filter-dump,id=capture,netdev=net0,file={capture}']
        elif mode == 'absent':
            args += ['-net', 'none']
        else:
            model = 'e1000' if mode == 'arp' else 'rtl8139'
            args += ['-netdev', 'user,id=net0,ipv6=off', '-device',
                     f'{model},netdev=net0,mac=52:54:00:12:34:56',
                     '-object', f'filter-dump,id=capture,netdev=net0,file={capture}']
        if firmware == 'uefi':
            ovmf = ROOT / 'boot/UEFI/OVMFbin'
            variables = Path(temporary) / 'OVMF_VARS.fd'
            shutil.copy2(ovmf / 'OVMF_VARS-pure-efi.fd', variables)
            args += ['-drive', f'if=pflash,format=raw,unit=0,file={ovmf}/OVMF_CODE-pure-efi.fd,readonly=on',
                     '-drive', f'if=pflash,format=raw,unit=1,file={variables}']
        expected = ('NET: ARP gateway timeout' if mode == 'silent' else
                    'NET: ARP gateway 10.0.2.2 resolved ' if mode in ('arp', 'peer') else
                    'NET: no supported NIC')
        with (output / f'{mode}.qemu.log').open('w') as log:
            process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
            try:
                if listener:
                    connection, _ = listener.accept()
                    connection.settimeout(timeout)
                    if mode == 'peer':
                        exercise_peer(connection, serial)
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    text = serial.read_text(errors='replace') if serial.exists() else ''
                    assert 'Page fault' not in text and 'PANIC' not in text, text
                    if expected in text:
                        break
                    if process.poll() is not None:
                        raise AssertionError(f'QEMU exited: {process.returncode}; see {output}')
                    time.sleep(0.1)
                else:
                    raise AssertionError(f'{mode} timed out; see {serial}')
            finally:
                if connection:
                    connection.close()
                if listener:
                    listener.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if mode == 'silent':
        assert serial.read_text().count('NET: ARP probe ') == 5
    if mode == 'arp':
        frames = packets(capture)
        mac = bytes.fromhex('525400123456')
        local = bytes([10, 0, 2, 15])
        gateway = bytes([10, 0, 2, 2])
        arp = [f for f in frames if len(f) >= 42 and f[12:22] == bytes.fromhex('08060001080006040001')]
        assert any(f[:6] == b'\xff' * 6 and f[6:12] == mac and f[22:28] == mac and
                   f[28:32] == local and f[32:38] == b'\0' * 6 and f[38:42] == gateway for f in arp), 'No valid guest ARP request'
        replies = [f for f in frames if len(f) >= 42 and f[12:22] == bytes.fromhex('08060001080006040002')]
        assert any(f[:6] == mac and f[28:32] == gateway and f[32:38] == mac and f[38:42] == local for f in replies), 'No gateway reply'
    print(f'PASS {firmware}: {mode}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/out/network')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--modes', nargs='+', choices=('arp', 'absent', 'unsupported', 'peer', 'silent'),
                        default=['arp', 'absent', 'unsupported', 'peer', 'silent'])
    args = parser.parse_args()
    for mode in args.modes:
        run(args.firmware, args.image, mode, args.output / args.firmware, args.timeout)
