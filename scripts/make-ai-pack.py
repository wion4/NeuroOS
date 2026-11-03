"""Package the optional pinned demo without network access or mutable model defaults."""
import hashlib
import json
from pathlib import Path
import struct
import zlib
def build(root:Path,target:Path):
    info=json.loads((root/'models.lock.json').read_text())['stories260k']
    files={}
    for name,record in info['files'].items():
        path=root/'tools/models/stories260k'/name
        if not path.is_file():raise RuntimeError('Optional AI assets missing; run python scripts/setup-ai-demo.py')
        data=path.read_bytes()
        if len(data)!=record['bytes'] or hashlib.sha256(data).hexdigest()!=record['sha256']:
            raise RuntimeError('Optional AI asset checksum failed: '+name)
        files[name]=data
    weights=files['stories260K.bin'];tokens=files['tok512.bin']
    pack=struct.pack('<8s6I',b'NOSAI001',len(weights),len(tokens),zlib.crc32(weights),zlib.crc32(tokens),0,0)+weights+tokens
    destination=target/'esp/boot/ai.pack';destination.write_bytes(pack)
    return destination
