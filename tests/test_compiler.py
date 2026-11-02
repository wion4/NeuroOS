"""Native guest compiler integration with a fresh, isolated virtual test disk."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib
from test_services import ServiceTests

ROOT=Path(__file__).resolve().parents[1]

def snapshot_files(disk):
    valid=[]
    with disk.open('rb') as stream:
        for bank in range(2):
            stream.seek((1024+bank*16)*512);root=bytearray(stream.read(8192))
            if struct.unpack_from('<Q',root)[0]!=0x3353464f5255454e:continue
            checksum=struct.unpack_from('<I',root,32)[0];struct.pack_into('<I',root,32,0)
            if zlib.crc32(root)==checksum:valid.append((struct.unpack_from('<Q',root,8)[0],root))
        if valid:
            _,root=max(valid);files={}
            for i in range(128):
                record=root[64+i*32:96+i*32]
                if not record[0]:continue
                name=record[:16].split(b'\0')[0].decode('ascii')
                length,start,capacity,checksum=struct.unpack_from('<4I',record,16)
                stream.seek(start*4096);data=stream.read(length)
                assert zlib.crc32(data)==checksum,'File CRC mismatch: '+name
                files[name]=data
            return files
        stream.seek(1023*512)
        if any(stream.read(512)):raise AssertionError('FS3 marker exists but roots are corrupt')
        for bank in range(2):
            stream.seek((64+bank*258)*512);data=stream.read(258*512)
            magic,generation,length,checksum,header_crc=struct.unpack_from('<5Q',data)
            payload=data[512:512+length]
            if (magic==0x3253464f5255454e and length==131264 and
                zlib.crc32(data[:32])==header_crc and zlib.crc32(payload)==checksum):
                valid.append((generation,payload))
    if not valid:raise AssertionError('No valid snapshot after native compilation')
    _,payload=max(valid)
    files={}
    for i in range(8):
        record=payload[i*16408:(i+1)*16408]
        if struct.unpack_from('<I',record,16)[0]:
            name=record[:16].split(b'\0')[0].decode('ascii')
            length=struct.unpack_from('<I',record,20)[0]
            files[name]=record[24:24+length]
    return files

def main():
    target=ROOT/'build/compiler-integration';target.mkdir(parents=True,exist_ok=True)
    report=target/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
    disk=target/'compiler-test-only.img'
    # This path belongs exclusively to this fixture, never the working VM disk.
    with disk.open('wb') as stream:stream.truncate(64*1024*1024)
    env=dict(os.environ,NEUROOS_FS_BACKEND='3',NEUROOS_DATA_DISK=str(disk))
    subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),'--compiler'],cwd=ROOT,env=env,check=True)
    files=snapshot_files(disk)
    ServiceTests.setUpClass();validator=ServiceTests()
    hashes={}
    for name in ('native.nos','asm.nos','raw.nos'):
        data=files[name]
        if not validator.valid(data)[0]:raise AssertionError('Kernel validator rejected guest output: '+name)
        (target/name).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
    subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),'--compiler-read'],cwd=ROOT,env=env,check=True)
    after=snapshot_files(disk)
    for name,digest in hashes.items():assert hashlib.sha256(after[name]).hexdigest()==digest
    assert after['native.nos']==after['second.nos'],'Repeated compilation was not reproducible'
    for mode in ('--compiler-installed','--compiler-installed-read'):
        subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),mode],cwd=ROOT,env=env,check=True)
    report.write_text(json.dumps({'passed':True,'guest_compiled':hashes,
        'checks':['C structs/loops/arrays/globals/double','integrated ASM',
                  'syntax/link/loader errors preserve existing output','temporary name collision',
                  'kernel ELF validation','cold boot execution','recompile after reboot',
                  'identical output bytes after reboot','compiler installed and executed from data disk',
                  'installed compiler works after cold boot','no physical-page leaks']},indent=2)+'\n')
    print('PASS: native C/ASM compiler, safe output replacement, cold boot and reproducible guest output')

if __name__=='__main__':main()
