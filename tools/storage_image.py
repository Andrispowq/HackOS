"""Offline provisioning for the version-1 HackOS FAT permission/journal extension.

Only image files are accepted. Never use this to repair a dirty journal or to
silently reset ownership on an existing filesystem.
"""
import hashlib
from pathlib import Path
import struct

RESERVED = 512
PERMISSION_START = 272
PERMISSION_SECTORS = 128


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def provision(image, offset=0, *, initialize=False):
    image = Path(image)
    if not image.is_file():
        raise ValueError('Storage provisioning requires a regular image file')
    with image.open('r+b') as stream:
        def read(position, size):
            stream.seek(offset + position)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError('Truncated filesystem')
            return data
        boot = read(0, 512)
        reserved = struct.unpack_from('<H', boot, 14)[0]
        if reserved != RESERVED:
            raise ValueError('Image lacks the reserved storage extension')
        header = read(8 * 512, 512)
        if initialize:
            if any(header):
                raise ValueError('Journal already initialized')
            header = bytearray(512)
            header[:16] = struct.pack('<8sII', b'HKFSJ016', 1, 0)
            header[48:80] = hashlib.sha256(header).digest()
            stream.seek(offset + 8 * 512)
            stream.write(header)
        else:
            if header[:16] != struct.pack('<8sII', b'HKFSJ016', 1, 0):
                raise ValueError('Refusing to provision an invalid or dirty journal')
            checked = bytearray(header)
            checked[16:80] = bytes(64)
            if hashlib.sha256(checked).digest() != header[48:80]:
                raise ValueError('Invalid journal checksum')
        table = bytearray(read(PERMISSION_START * 512, PERMISSION_SECTORS * 512))
        existing = {}
        free = []
        for i in range(2048):
            record = table[i * 32:(i + 1) * 32]
            if not any(record):
                free.append(i)
                continue
            tag, parent, slot, uid, gid, mode, reserved, digest = struct.unpack('<8I', record)
            if tag != 0x36315048 or mode > 0o777 or reserved or checksum(record[:28]) != digest or (parent, slot) in existing:
                raise ValueError('Invalid permission record')
            existing[parent, slot] = i
        def add(parent, slot, path, directory):
            if (parent, slot) in existing:
                return
            if not free:
                raise ValueError('Permission table full')
            mode = 0o755 if directory or path.startswith('/USR/BIN/') else 0o644
            if path == '/TMP':
                mode = 0o777
            record = struct.pack('<7I', 0x36315048, parent, slot, 0, 0, mode, 0)
            i = free.pop(0)
            table[i * 32:(i + 1) * 32] = record + struct.pack('<I', checksum(record))
            existing[parent, slot] = i
        sectors_per_cluster = boot[13]
        fat_size = struct.unpack_from('<I', boot, 36)[0]
        first = RESERVED + boot[16] * fat_size
        total = struct.unpack_from('<I', boot, 32)[0]
        clusters = (total - first) // sectors_per_cluster
        fat = read(RESERVED * 512, fat_size * 512)
        visited = set()
        def walk(cluster, path):
            chain = set()
            while cluster < 0xffffff8:
                if cluster < 2 or cluster >= clusters + 2 or cluster in chain or cluster in visited:
                    raise ValueError('Invalid directory chain')
                chain.add(cluster)
                visited.add(cluster)
                raw = read((first + (cluster - 2) * sectors_per_cluster) * 512, sectors_per_cluster * 512)
                for slot in range(len(raw) // 32):
                    entry = raw[slot * 32:(slot + 1) * 32]
                    if entry[0] == 0:
                        return
                    if entry[0] in (0xe5, 46) or entry[11] & 8:
                        continue
                    name = entry[:8].decode('ascii').rstrip() + ('.' + entry[8:11].decode('ascii').rstrip() if entry[8:11].strip() else '')
                    child_path = path + '/' + name.upper()
                    directory = bool(entry[11] & 16)
                    add(cluster, slot, child_path, directory)
                    if directory:
                        child = struct.unpack_from('<H', entry, 26)[0] | struct.unpack_from('<H', entry, 20)[0] << 16
                        walk(child, child_path)
                cluster = struct.unpack_from('<I', fat, cluster * 4)[0] & 0xfffffff
        add(0, 0, '', True)
        walk(struct.unpack_from('<I', boot, 44)[0], '')
        stream.seek(offset + PERMISSION_START * 512)
        stream.write(table)


def format_volume(image, sectors=93750):
    # Ubuntu's mtools bounds -R to 255. Format a smaller standard volume, then
    # insert the extension before the FATs, preserving every cluster index.
    import subprocess
    image = Path(image)
    with image.open('wb') as stream:
        stream.truncate((sectors - (RESERVED - 32)) * 512)
    subprocess.run(['mformat', '-i', str(image), '-F', '-R', '32', '::'], check=True)
    original = image.read_bytes()
    if struct.unpack_from('<H', original, 14)[0] != 32:
        raise ValueError('Unexpected mtools reserved-sector layout')
    prefix = bytearray(original[:32 * 512]) + bytearray((RESERVED - 32) * 512)
    backup = struct.unpack_from('<H', original, 50)[0]
    if backup != 6 or struct.unpack_from('<H', original, 48)[0] != 1:
        raise ValueError('Unexpected mtools boot/FSInfo locations')
    for position in (0, backup * 512):
        struct.pack_into('<H', prefix, position + 14, RESERVED)
        struct.pack_into('<I', prefix, position + 32, sectors)
    with image.open('wb') as stream:
        stream.write(prefix)
        stream.write(original[32 * 512:])
    if image.stat().st_size != sectors * 512:
        raise ValueError('Invalid expanded volume size')
