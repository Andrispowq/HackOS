"""Copy opaque account slots between image files without resetting damaged state."""
import struct
import subprocess
import tempfile
from pathlib import Path


def filesystem(image):
    with Path(image).open('rb') as source:
        sector = source.read(512)
    if len(sector) != 512 or sector[510:] != b'\x55\xaa':
        raise ValueError('Invalid image boot sector')
    if sector[82:90] == b'FAT32   ':
        return str(image)
    for index in range(4):
        entry = sector[446 + index * 16:462 + index * 16]
        if entry[4] in (0x0b, 0x0c):
            return str(image) + '@@' + str(struct.unpack_from('<I', entry, 8)[0] * 512)
    raise ValueError('Image has no FAT32 partition')


def read_accounts(image):
    fs = filesystem(image)
    slots = []
    for slot in (0, 1):
        result = subprocess.run(['mcopy', '-i', fs, f'::/SYSTEM/ACCT{slot}.DAT', '-'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        if len(result.stdout) != 8192:
            raise ValueError('Account slot has an invalid size; keep the original image for recovery')
        slots.append(result.stdout)
    return slots


def write_accounts(image, slots):
    if len(slots) != 2 or any(len(slot) != 8192 for slot in slots):
        raise ValueError('Two complete account slots are required')
    fs = filesystem(image)
    with tempfile.TemporaryDirectory(prefix='hackos-accounts-') as temporary:
        source = Path(temporary) / 'accounts.dat'
        for slot, data in enumerate(slots):
            destination = f'::/SYSTEM/ACCT{slot}.DAT'
            source.write_bytes(data)
            subprocess.run(['mattrib', '-i', fs, '-h', '-s', destination], check=True)
            subprocess.run(['mcopy', '-o', '-i', fs, str(source), destination], check=True)
            subprocess.run(['mattrib', '-i', fs, '+h', '+s', destination], check=True)
    if read_accounts(image) != slots:
        raise ValueError('Account copy verification failed')
