#!/usr/bin/env python3
"""Exercise a non-default DHCP subnet, renewal, rebinding, expiry, NAK and link recovery."""
import argparse
from dhcp_peer import wait_serial
from pathlib import Path
import shutil
import struct
from common import OUT
from qemu_accounts import boot, ADMIN_PASSWORD
from qemu_routing import Router, GUEST_MAC
from dhcp_peer import DhcpPeer, udp_frame, checksum

ADDRESS = bytes([198, 18, 7, 129])
MASK = bytes([255, 255, 255, 128])
GATEWAY = bytes([198, 18, 7, 130])
DNS = bytes([198, 18, 7, 131])
NEIGHBOR = bytes([198, 18, 7, 200])
REMOTE = bytes([198, 51, 100, 9])
PEER_MAC = bytes.fromhex('525500000067')
NEIGHBOR_MAC = bytes.fromhex('525500000068')


class LeaseRouter(Router):
    def __init__(self):
        super().__init__()
        self.dhcp = DhcpPeer(ADDRESS, MASK, GATEWAY, DNS, lease=16, renew=8, rebind=14)
        self.routed = []

    def dhcp_send(self, frame):
        payload = bytearray(frame[42:])
        # Valid UDP but wrong xid must not poison the selected address.
        bad = bytearray(payload)
        bad[4] ^= 1
        bad[19] = 150
        self.send(udp_frame(frame[:6], PEER_MAC, GATEWAY, frame[30:34], 67, 68, bad))
        # A noncontiguous mask is invalid even with the correct xid and checksums.
        if payload[242] == 2:
            bad = bytearray(payload)
            at = 240
            while bad[at] != 255:
                code, size = bad[at:at + 2]
                if code == 1:
                    bad[at + 5] = 129
                    break
                at += 2 + size
            bad[19] = 150
            self.send(udp_frame(frame[:6], PEER_MAC, GATEWAY, frame[30:34], 67, 68, bad))
        self.send(frame)

    def handle(self, frame):
        if self.dhcp.handle(self.dhcp_send, frame):
            return
        if frame[12:14] == b'\x08\x06':
            if frame[20:22] != b'\0\1':
                return
            target = frame[38:42]
            assert frame[28:32] == ADDRESS
            assert target in (GATEWAY, DNS, NEIGHBOR), 'ARP leaked off the leased subnet'
            mac = NEIGHBOR_MAC if target == NEIGHBOR else PEER_MAC
            reply = GUEST_MAC + mac + bytes.fromhex('08060001080006040002') + mac + target + GUEST_MAC + ADDRESS
            self.send(reply.ljust(60, b'\0'))
            return
        if frame[12:14] != b'\x08\0':
            return
        assert frame[26:30] == ADDRESS and checksum(frame[14:34]) == 0
        target = frame[30:34]
        assert frame[:6] == (NEIGHBOR_MAC if target == NEIGHBOR else PEER_MAC)
        self.routed.append(target)
        total = struct.unpack('!H', frame[16:18])[0]
        if frame[23] == 1:
            data = bytearray(frame[34:14 + total])
            assert data[0] == 8 and checksum(data) == 0
            data[0] = 0
            data[2:4] = b'\0\0'
            struct.pack_into('!H', data, 2, checksum(data))
            ip = bytearray(frame[14:34])
            ip[12:16], ip[16:20] = target, ADDRESS
            ip[10:12] = b'\0\0'
            struct.pack_into('!H', ip, 10, checksum(ip))
            self.send(GUEST_MAC + frame[:6] + b'\x08\0' + ip + data)
            return
        assert frame[23] == 17
        udp = frame[34:14 + total]
        local_port, port = struct.unpack('!HH', udp[:4])
        assert checksum(ADDRESS + target + b'\0\x11' + struct.pack('!H', len(udp)) + udp) == 0
        data = udp[8:]
        if port == 53:
            assert target == DNS
            answer = data[:2] + bytes.fromhex('81800001000100000000') + data[12:]
            answer += b'\xc0\x0c' + struct.pack('!HHIH', 1, 1, 60, 4) + REMOTE
            data = answer
        else:
            assert port == 7777 and target == REMOTE
        self.send(udp_frame(GUEST_MAC, frame[:6], target, ADDRESS, port, local_port, data))


def test(firmware, image, timeout):
    directory = OUT / ('dhcp-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    router = LeaseRouter()
    try:
        with boot(directory, disposable, firmware, 'dhcp', timeout, qmp_stdio=True, network=True,
                  capture=directory / 'dhcp.pcap', network_backend=f'socket,id=net0,connect=127.0.0.1:{router.port}') as guest:
            guest.fresh = True
            guest.setup()
            guest.enter('root', 'New password: ')
            guest.password(ADMIN_PASSWORD, 'Action: ')
            guest.login('root', ADMIN_PASSWORD)
            start = len(guest.output())
            guest.shell('sysinfo network', 'DHCP server')
            text = guest.output()[start:]
            for expected in ('198.18.7.129', '255.255.255.128', '198.18.7.130', '198.18.7.131'):
                assert expected in text, text
            guest.shell('ping -c 1 198.18.7.200', '1 probes, 1 received, 0% loss')
            guest.shell('runas guest ping -c 1 198.51.100.9', '1 probes, 1 received, 0% loss')
            guest.shell('udp demo.test 7777 leased', 'Received 6 bytes: leased')
            guest.screen('leased-network')
            offset = len(guest.serial.read_text(errors="replace"))
            wait_serial(guest, 'NET: DHCP lease renewed', offset)
            router.dhcp.mode = 'rebind'
            offset = len(guest.serial.read_text(errors="replace"))
            wait_serial(guest, 'NET: DHCP rebinding', offset)
            wait_serial(guest, 'NET: DHCP lease renewed', offset)
            assert (3, ADDRESS, b'\xff' * 4) in router.dhcp.requests
            router.dhcp.mode = 'silent'
            offset = len(guest.serial.read_text(errors="replace"))
            wait_serial(guest, 'NET: DHCP lease expired', offset)
            start = len(guest.output())
            guest.shell('sysinfo network', 'DHCP server')
            assert '198.18.7.129' not in guest.output()[start:]
            guest.shell('udp 198.51.100.9 7777 offline', 'network unavailable')
            router.dhcp.mode = 'normal'
            offset = len(guest.serial.read_text(errors="replace"))
            wait_serial(guest, 'NET: DHCP lease bound', offset)
            router.dhcp.mode = 'nak'
            offset = len(guest.serial.read_text(errors="replace"))
            wait_serial(guest, 'NET: DHCP NAK; restarting', offset)
            router.dhcp.mode = 'normal'
            wait_serial(guest, 'NET: DHCP lease bound', offset)
            guest.qmp.call('set_link', {'name': 'net0', 'up': False})
            guest.shell('udp 198.51.100.9 7777 offline', 'network unavailable')
            offset = len(guest.serial.read_text(errors="replace"))
            guest.qmp.call('set_link', {'name': 'net0', 'up': True})
            wait_serial(guest, 'NET: DHCP lease bound', offset)
            guest.shell('udp demo.test 7777 recovered', 'Received 9 bytes: recovered')
        assert router.failure is None, repr(router.failure)
        assert all(target in router.routed for target in (NEIGHBOR, DNS, REMOTE))
        print(f'PASS {firmware}: DHCP dynamic /25, gateway/DNS, renewal, rebinding, expiry, NAK, stale/malformed offers and link recovery')
    finally:
        router.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout)
