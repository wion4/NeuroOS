"""Cold-boot migration using a disposable disk, never the working VM disk."""
import os,struct,subprocess,sys,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
target=ROOT/'build/migration';target.mkdir(parents=True,exist_ok=True)
disk=target/'test-data.img'
payload=bytearray(8384);payload[:7]=b'legacy\0';payload[16]=1
text=b'preserved from v1';struct.pack_into('<I',payload,20,len(text));payload[24:24+len(text)]=text
header=bytearray(512);struct.pack_into('<QQQQ',header,0,0x3153464f5255454e,7,8384,zlib.crc32(payload))
struct.pack_into('<Q',header,32,zlib.crc32(header[:32]))
with disk.open('wb') as f:
    f.truncate(64*1024*1024);f.seek(0);f.write(header+payload)
    f.seek(65*512);f.write(b'interrupted first v2 migration')
original=disk.read_bytes()[:36*512]
env=os.environ.copy();env['NEUROOS_DATA_DISK']=str(disk)
env['NEUROOS_FS_BACKEND']='2' # Historical v1->v2 regression; v3 has its own guest test.
subprocess.run([sys.executable,'tests/test_shell.py','--migrate'],cwd=ROOT,env=env,check=True)
with disk.open('r+b') as f:
    assert f.read(36*512)==original,'Legacy banks were overwritten'
    banks=[]
    for i in range(2):
        f.seek((64+i*258)*512);banks.append(bytearray(f.read(258*512)))
    current=max(banks,key=lambda b:struct.unpack_from('<Q',b,8)[0])
    assert struct.unpack_from('<Q',current)[0]==0x3253464f5255454e
    # Add an executable larger than the old 1 KiB limit to the disposable v2 image.
    code=bytearray((ROOT/'build/normal/esp/boot/hello.nos').read_bytes())
    code+=bytes(3072-len(code));struct.pack_into('<QQ',code,64+32,3072,3072)
    record=512+2*16408
    current[record:record+16408]=bytes(16408)
    current[record:record+8]=b'big.nos\0';current[record+16]=1
    struct.pack_into('<I',current,record+20,len(code));current[record+24:record+24+len(code)]=code
    struct.pack_into('<Q',current,8,struct.unpack_from('<Q',current,8)[0]+1)
    struct.pack_into('<Q',current,24,zlib.crc32(current[512:512+131264]))
    struct.pack_into('<Q',current,32,zlib.crc32(current[:32]))
    f.seek(64*512);f.write(current)
subprocess.run([sys.executable,'tests/test_shell.py','--migration-read'],cwd=ROOT,env=env,check=True)
with disk.open('r+b') as f:
    for bank in (0,1):
        f.seek((64+bank*258)*512);byte=f.read(1)
        f.seek(-1,1);f.write(bytes([byte[0]^1]))
subprocess.run([sys.executable,'tests/test_shell.py','--corrupt-check'],cwd=ROOT,env=env,check=True)
print('PASS: migration preserves original v1 bytes and loads larger persisted executables')
