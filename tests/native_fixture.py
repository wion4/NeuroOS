"""Own disposable legacy fixture, imported by the actual FS3 guest service."""
from pathlib import Path
import struct
import zlib
ROOT = Path(__file__).resolve().parents[1]

def seed_native_disk(disk, target):
    disk = disk.resolve()
    assert disk.is_relative_to(ROOT / 'build') and disk.name.endswith('-test-only.img')
    hello = (target / 'esp/boot/hello.nos').read_bytes()
    spin = bytearray(hello); spin[128:130] = b'\xeb\xfe'
    files = {'hello.nos': hello, 'spin.nos': bytes(spin),
             'filedemo.nos': (target / 'esp/boot/filedemo.nos').read_bytes(),
             'sentinel.txt': b'seed'}
    payload = bytearray(131264)
    for index, (name, data) in enumerate(files.items()):
        assert len(data) <= 16384
        offset = index * 16408
        payload[offset:offset + len(name)] = name.encode()
        struct.pack_into('<II', payload, offset + 16, 1, len(data))
        payload[offset + 24:offset + 24 + len(data)] = data
    header = bytearray(512)
    struct.pack_into('<4Q', header, 0, 0x3253464f5255454e, 3, len(payload), zlib.crc32(payload))
    struct.pack_into('<Q', header, 32, zlib.crc32(header[:32]))
    with disk.open('wb') as stream:
        stream.truncate(64 * 1024 * 1024)
        stream.seek(64 * 512); stream.write(header + payload)
    return files
