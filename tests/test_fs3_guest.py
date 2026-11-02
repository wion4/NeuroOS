"""Exercise experimental v3 in the real guest with a separate v2 seed image."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib
ROOT=Path(__file__).resolve().parents[1]

def main():
    target=ROOT/'build/fs3-integration';target.mkdir(parents=True,exist_ok=True)
    report=target/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
    payload=bytearray(131264)
    hello=(ROOT/'build/normal/esp/boot/hello.nos').read_bytes()
    for index,name,data in ((0,b'legacy',b'old v2 data preserved'),(1,b'oldprog.nos',hello)):
        offset=index*16408;payload[offset:offset+len(name)]=name
        struct.pack_into('<II',payload,offset+16,1,len(data));payload[offset+24:offset+24+len(data)]=data
    header=bytearray(512)
    struct.pack_into('<4Q',header,0,0x3253464f5255454e,3,len(payload),zlib.crc32(payload))
    struct.pack_into('<Q',header,32,zlib.crc32(header[:32]))
    disk=target/'fs3-test-only.img'
    with disk.open('wb') as stream:
        stream.truncate(64*1024*1024);stream.seek(64*512);stream.write(header+payload)
        stream.seek(63*512);stream.write(b'committed-v2')
    with disk.open('rb') as stream:legacy=stream.read(1023*512)
    env=dict(os.environ,NEUROOS_FS_BACKEND='3',NEUROOS_FS3_TEST='1',NEUROOS_DATA_DISK=str(disk))
    for mode in ('--fs3-write','--fs3-read'):
        subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),mode],cwd=ROOT,env=env,check=True)
        with disk.open('rb') as stream:assert stream.read(1023*512)==legacy,'Legacy sectors changed'
    # Deliberately damage only this fixture after both healthy guest boots.
    with disk.open('r+b') as stream:
        for sector in (1024,1040):
            stream.seek(sector*512);first=stream.read(1);stream.seek(sector*512);stream.write(bytes([first[0]^1]))
    subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),'--fs3-corrupt'],cwd=ROOT,env=env,check=True)
    with disk.open('rb') as stream:assert stream.read(1023*512)==legacy
    report.write_text(json.dumps({'passed':True,'legacy_prefix_bytes_preserved':len(legacy),
        'checks':['v2 import','old NOS execution','1 MiB sparse file','40 additional names',
                  'cold restart','filesystem crash rollback and restart','both roots corrupt fail closed']},indent=2)+'\n')
    print('PASS: experimental FS3 real-guest migration, large file, restart, recovery and fail-closed corruption')

if __name__=='__main__':main()
