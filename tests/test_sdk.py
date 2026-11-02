"""Install and use the real SDK inside two cold guest boots, isolated disk."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib
from test_compiler import snapshot_files
ROOT=Path(__file__).resolve().parents[1]

def main():
    target=ROOT/'build/sdk-integration';target.mkdir(parents=True,exist_ok=True)
    report=target/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
    disk=target/'sdk-test-only.img'
    with disk.open('wb') as stream:stream.truncate(64*1024*1024)
    env=dict(os.environ,NEUROOS_FS_BACKEND='3',NEUROOS_DATA_DISK=str(disk))
    for mode in ('--sdk','--sdk-read'):
        subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),mode],cwd=ROOT,env=env,check=True)
    files=snapshot_files(disk)
    assert len(files)>8
    assert files['sdkdemo.nos']==files['sdkagain.nos']
    pack=(ROOT/'build/normal/esp/boot/sdk.pack').read_bytes()
    magic,count,version,length,checksum,reserved=struct.unpack_from('<8sIIQII',pack)
    assert magic==b'NOSSDK01' and count==21 and length==len(pack) and not reserved
    assert zlib.crc32(pack[32:])==checksum
    for i in range(count):
        name,offset,size,checksum,reserved=struct.unpack_from('<16s4I',pack,32+i*32)
        name=name.split(b'\0')[0].decode()
        assert files[name]==pack[offset:offset+size],name
        assert zlib.crc32(files[name])==checksum
    report.write_text(json.dumps({'passed':True,'installed_files':count,
        'checks':['existing header never overwritten','no partial install on name conflict',
                  'installed bytes match package','native include and static library link',
                  'TCC caller -> LLVM library ABI','printf/malloc/strtod/files',
                  'run denies file operations; runfs grants them','cold restart and identical rebuild']},indent=2)+'\n')
    print('PASS: guest-installed C SDK, native compilation/linking, permissions, cold boot and byte-identical output')

if __name__=='__main__':main()
