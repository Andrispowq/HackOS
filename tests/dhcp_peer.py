"""Deterministic DHCP server shared by loopback-only network fixtures."""
import struct
import time


def checksum(data):
    data += b'\0' if len(data) % 2 else b''
    value = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def udp_frame(mac, peer_mac, source, target, source_port, target_port, payload):
    udp = bytearray(struct.pack('!HHHH', source_port, target_port, len(payload) + 8, 0) + payload)
    struct.pack_into('!H', udp, 6, checksum(source + target + b'\0\x11' + struct.pack('!H', len(udp)) + udp) or 65535)
    ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, len(udp) + 20, 0, 0x4000, 64, 17, 0, source, target))
    struct.pack_into('!H', ip, 10, checksum(ip))
    return (mac + peer_mac + b'\x08\0' + ip + udp).ljust(60, b'\0')


class DhcpPeer:
    def __init__(self, address=bytes([10, 0, 2, 15]), mask=bytes([255, 255, 255, 0]),
                 gateway=bytes([10, 0, 2, 2]), dns=bytes([10, 0, 2, 3]), lease=3600, renew=None, rebind=None):
        self.address, self.mask, self.gateway, self.dns = address, mask, gateway, dns
        self.server = gateway
        self.lease, self.renew, self.rebind = lease, renew, rebind
        self.mac = bytes.fromhex('525500000067')
        self.requests = []
        self.mode = 'normal'

    def handle(self, send, frame):
        if len(frame) < 42 or frame[12:14] != b'\x08\0' or frame[23] != 17 or frame[34:38] != b'\0\x44\0\x43':
            return False
        total = struct.unpack('!H', frame[16:18])[0]
        udp = frame[34:14 + total]
        assert checksum(frame[14:34]) == 0
        assert checksum(frame[26:34] + b'\0\x11' + struct.pack('!H', len(udp)) + udp) == 0
        query = udp[8:]
        assert len(query) >= 240 and query[:3] == bytes([1, 1, 6]) and query[236:240] == bytes.fromhex('63825363')
        options, at = {}, 240
        while query[at] != 255:
            code = query[at]
            at += 1
            if not code:
                continue
            size = query[at]
            options[code] = query[at + 1:at + 1 + size]
            at += 1 + size
        kind = options[53][0]
        assert kind in (1, 3)
        ciaddr = query[12:16]
        self.requests.append((kind, ciaddr, frame[30:34]))
        if self.mode == 'silent' or (self.mode == 'rebind' and ciaddr != bytes(4) and frame[30:34] != b'\xff' * 4):
            return True
        if kind == 3 and ciaddr == bytes(4):
            assert options[50] == self.address and options[54] == self.server
        if ciaddr != bytes(4):
            assert ciaddr == self.address and 50 not in options and 54 not in options
        payload = bytearray(240)
        payload[:3] = bytes([2, 1, 6])
        payload[4:8] = query[4:8]
        payload[16:20] = self.address
        payload[28:34] = query[28:34]
        payload[236:240] = bytes.fromhex('63825363')
        reply_kind = 2 if kind == 1 else 6 if self.mode == 'nak' else 5
        payload += bytes([53, 1, reply_kind, 54, 4]) + self.server
        for code, value in [(1, self.mask), (3, self.gateway), (6, self.dns), (51, struct.pack('!I', self.lease))]:
            payload += bytes([code, len(value)]) + value
        for code, value in [(58, self.renew), (59, self.rebind)]:
            if value is not None:
                payload += bytes([code, 4]) + struct.pack('!I', value)
        payload += b'\xff'
        target = ciaddr if ciaddr != bytes(4) else b'\xff' * 4
        target_mac = query[28:34] if ciaddr != bytes(4) else b'\xff' * 6
        send(udp_frame(target_mac, self.mac, self.server, target, 67, 68, payload))
        return True


def wait_serial(guest, text, offset=0):
    deadline = time.monotonic() + guest.timeout
    while time.monotonic() < deadline:
        output = guest.serial.read_text(errors='replace')
        assert guest.process.poll() is None, 'QEMU exited: ' + output[-1000:]
        if text in output[offset:]:
            return
        time.sleep(0.05)
    raise AssertionError('Timed out waiting for serial ' + repr(text) + ': ' + output[-2000:])
