"""Experimental backend tests. IO is a sparse sector model, never a host disk."""
import ctypes as c
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zlib
from test_services import Message
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

READ=c.CFUNCTYPE(c.c_bool,c.c_void_p,c.c_uint32,c.c_void_p)
WRITE=c.CFUNCTYPE(c.c_bool,c.c_void_p,c.c_uint32,c.c_void_p)
FLUSH=c.CFUNCTYPE(c.c_bool,c.c_void_p)
class IO(c.Structure):
    _fields_=[('context',c.c_void_p),('read',READ),('write',WRITE),('flush',FLUSH)]

class Disk:
    def __init__(self,sectors=None,write_through=False):
        self.sectors=dict(sectors or {});self.writes=0;self.flushes=0
        self.durable=dict(self.sectors);self.write_through=write_through;self.torn=False
        self.fail_write=0;self.fail_flush=0;self.illegal=[]
        @READ
        def read(context,sector,buffer):
            if sector>=131072:return False
            c.memmove(buffer,self.sectors.get(sector,bytes(512)),512);return True
        @WRITE
        def write(context,sector,buffer):
            if sector<1023 or sector>=131072:
                self.illegal.append(sector);return False
            self.writes+=1
            data=c.string_at(buffer,512)
            if self.fail_write==self.writes:
                if self.torn:
                    self.sectors[sector]=data[:128]+self.sectors.get(sector,bytes(512))[128:]
                    if self.write_through:self.durable[sector]=self.sectors[sector]
                return False
            self.sectors[sector]=data
            if self.write_through:self.durable[sector]=data
            return True
        @FLUSH
        def flush(context):
            self.flushes+=1
            if self.fail_flush==self.flushes:return False
            self.durable=dict(self.sectors);return True
        self.callbacks=(read,write,flush);self.io=IO(None,*self.callbacks)
    def root(self,bank):return b''.join(self.sectors.get(1024+bank*16+i,bytes(512)) for i in range(16))
    def replace_root(self,bank,data):
        for i in range(16):self.sectors[1024+bank*16+i]=bytes(data[i*512:(i+1)*512])
    def crash(self):self.sectors=dict(self.durable)

class FS3Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/fs3-tests';target.mkdir(parents=True,exist_ok=True)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in ('user/fs3.c','tests/fs3_harness.c','kernel/runtime.c'):
            obj=target/(Path(source).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding',
                            '-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror',
                            '-c',source,'-o',str(obj)],cwd=ROOT,check=True);objects.append(str(obj))
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
                        '/export:fs3_mount','/export:fs3_handle','/export:fs3_sizeof','/export:fs3_import_legacy',
                        '/export:fs3_handle_client','/export:fs3_client_closed',
                        '/out:'+str(target/'fs3.dll')],check=True)
        cls.lib=c.CDLL(str(target/'fs3.dll'));cls.lib.fs3_sizeof.restype=c.c_size_t
        cls.lib.fs3_mount.argtypes=[c.c_void_p,c.POINTER(IO)];cls.lib.fs3_mount.restype=c.c_int
        cls.lib.fs3_handle.argtypes=[c.c_void_p,c.POINTER(Message),c.POINTER(Message)]
        cls.lib.fs3_handle_client.argtypes=[c.c_void_p,c.c_uint64,c.POINTER(Message),c.POINTER(Message)]
        cls.lib.fs3_handle_client.restype=None
        cls.lib.fs3_client_closed.argtypes=[c.c_void_p,c.c_uint64]
        cls.lib.fs3_client_closed.restype=c.c_bool
        cls.lib.fs3_import_legacy.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t]
        cls.lib.fs3_import_legacy.restype=c.c_int
    def mount(self,disk):
        volume=c.create_string_buffer(self.lib.fs3_sizeof())
        return self.lib.fs3_mount(volume,c.byref(disk.io)),volume
    def setUp(self):
        self.disk=Disk();result,self.volume=self.mount(self.disk);self.assertEqual(result,0)
    def request(self,op,name='file',offset=0,data=b'',size=None,volume=None,client=None):
        if size is None:size=len(data)
        q=Message(op=op,name=name.encode(),offset=offset,size=size);r=Message()
        if data:c.memmove(c.addressof(q)+Message.data.offset,data,len(data))
        target=volume if volume is not None else self.volume
        if client is None:self.lib.fs3_handle(target,c.byref(q),c.byref(r))
        else:self.lib.fs3_handle_client(target,client,c.byref(q),c.byref(r))
        return r
    def read(self,name='file',offset=0,size=32,volume=None):
        r=self.request(3,name,offset,size=size,volume=volume)
        self.assertEqual(r.status,0);return c.string_at(c.addressof(r)+Message.data.offset,r.size)
    def replace(self,data,name='file'):
        self.assertEqual(self.request(10).status,0)
        r=self.request(1,name);self.assertIn(r.status,(0,-17))
        self.assertEqual(self.request(8,name).status,0)
        for offset in range(0,len(data),32):self.assertEqual(self.request(2,name,offset,data[offset:offset+32]).status,0)
        self.assertEqual(self.request(11).status,0)
    def test_basic_cold_mount_and_old_area_untouched(self):
        self.replace(b'persisted')
        result,other=self.mount(self.disk);self.assertEqual(result,1)
        self.assertEqual(self.read(volume=other),b'persisted')
        self.assertFalse(self.disk.illegal)
        self.assertTrue(all(sector>=1023 for sector in self.disk.sectors))
    def test_abort_and_read_own_pending_writes(self):
        self.replace(b'original')
        self.assertEqual(self.request(10).status,0)
        self.assertEqual(self.request(2,data=b'changed!').status,0)
        self.assertEqual(self.read(),b'changed!')
        self.assertEqual(self.request(12).status,0)
        self.assertEqual(self.read(),b'original')
        self.assertEqual(self.request(12).status,-22)
    def test_transaction_client_cannot_read_or_control_another_draft(self):
        self.replace(b'original')
        owner,other=0x100000007,0x200000009
        self.assertEqual(self.request(10,client=owner).status,0)
        self.assertEqual(self.request(2,data=b'private!',client=owner).status,0)
        before=(self.disk.writes,self.disk.flushes,dict(self.disk.sectors))
        for op in range(1,13):
            self.assertEqual(self.request(op,client=other).status,-16,op)
        self.assertEqual(self.request(3).status,-16) # Legacy caller cannot bypass ownership.
        self.assertEqual((self.disk.writes,self.disk.flushes,self.disk.sectors),before)
        own=self.request(3,size=32,client=owner)
        self.assertEqual(own.status,0);self.assertEqual(bytes(own.data[:own.size]),b'private!')
        self.assertEqual(self.request(11,client=owner).status,0)
        committed=self.request(3,size=32,client=other)
        self.assertEqual(committed.status,0);self.assertEqual(bytes(committed.data[:committed.size]),b'private!')
    def test_client_death_rolls_back_only_its_transaction_without_io(self):
        self.replace(b'committed')
        self.assertEqual(self.request(10,client=41).status,0)
        self.assertEqual(self.request(2,data=b'pending!!',client=41).status,0)
        before=(self.disk.writes,self.disk.flushes,dict(self.disk.sectors))
        self.assertFalse(self.lib.fs3_client_closed(self.volume,42))
        self.assertTrue(self.lib.fs3_client_closed(self.volume,41))
        self.assertFalse(self.lib.fs3_client_closed(self.volume,41))
        self.assertEqual((self.disk.writes,self.disk.flushes,self.disk.sectors),before)
        self.assertEqual(self.read(),b'committed')
        status,cold=self.mount(self.disk);self.assertEqual(status,1)
        self.assertEqual(self.read(volume=cold),b'committed')
    def test_stale_client_identity_cannot_abort_reused_slot(self):
        self.replace(b'old')
        old,new=0x100000001,0x200000001
        self.assertEqual(self.request(10,client=old).status,0)
        self.assertTrue(self.lib.fs3_client_closed(self.volume,old))
        self.assertEqual(self.request(10,client=new).status,0)
        self.assertEqual(self.request(2,data=b'new',client=new).status,0)
        self.assertFalse(self.lib.fs3_client_closed(self.volume,old))
        self.assertEqual(self.request(12,client=old).status,-16)
        self.assertEqual(self.request(11,client=old).status,-16)
        self.assertEqual(self.request(11,client=new).status,0)
        self.assertEqual(self.read(),b'new')
        self.assertEqual(self.request(1,name='auto-a',client=old).status,0)
        self.assertEqual(self.request(1,name='auto-b',client=new).status,0)
    def test_client_cleanup_does_not_unpoison_failed_commit(self):
        self.replace(b'original')
        self.assertEqual(self.request(10,client=7).status,0)
        self.assertEqual(self.request(2,data=b'not safe',client=7).status,0)
        self.disk.fail_write=self.disk.writes+1
        self.assertEqual(self.request(11,client=7).status,-5)
        before=(self.disk.writes,self.disk.flushes)
        self.assertTrue(self.lib.fs3_client_closed(self.volume,7))
        self.assertEqual(self.request(1,name='forbidden',client=8).status,-5)
        self.assertEqual((self.disk.writes,self.disk.flushes),before)
        self.disk.crash();status,cold=self.mount(self.disk);self.assertEqual(status,1)
        self.assertEqual(self.read(volume=cold),b'original')
    def test_holes_cross_blocks_and_truncate(self):
        self.replace(b'head')
        self.assertEqual(self.request(10).status,0)
        self.assertEqual(self.request(2,offset=4090,data=b'cross the boundary').status,0)
        self.assertEqual(self.request(11).status,0)
        self.assertEqual(self.read(offset=4000),bytes(32))
        self.assertEqual(self.read(offset=4090),b'cross the boundary')
        self.assertEqual(self.request(8).status,0)
        self.assertEqual(self.read(),b'')
        self.assertEqual(self.request(2,offset=8,data=b'X').status,0)
        self.assertEqual(self.read(),bytes(8)+b'X')
    def test_directory_128_and_reuse(self):
        for i in range(128):self.assertEqual(self.request(1,f'f{i}').status,0)
        self.assertEqual(self.request(1,'overflow').status,-28)
        self.assertEqual(self.request(4,'f70').status,0)
        self.assertEqual(self.request(1,'reused').status,0)
        r=self.request(5,offset=70);self.assertEqual(r.name,b'reused');self.assertEqual(r.offset,71)
    def test_one_megabyte_and_atomic_size_rejection(self):
        data=bytes(range(256))*4096
        self.replace(data)
        self.assertEqual(self.request(6).size,1048576)
        self.assertEqual(self.read(offset=1048544),data[-32:])
        before=dict(self.disk.sectors)
        self.assertEqual(self.request(2,offset=1048575,data=b'XX').status,-27)
        self.assertEqual(self.disk.sectors,before)
        result,other=self.mount(self.disk);self.assertEqual(result,1)
        self.assertEqual(self.read(offset=524280,size=32,volume=other),data[524280:524312])
    def test_power_cut_at_every_sector_write(self):
        self.replace(b'old value')
        initial=dict(self.disk.sectors)
        # Existing marker is never rewritten. A transaction needs 8 extent,
        # 8 cache and 16 root writes; every failure must leave old or new data.
        for write_through in (False,True):
            for torn in (False,True):
                for cut in range(1,33):
                    with self.subTest(cut=cut,torn=torn,write_through=write_through):
                        disk=Disk(initial,write_through);disk.torn=torn
                        result,volume=self.mount(disk);self.assertEqual(result,1)
                        self.assertEqual(self.request(10,volume=volume).status,0)
                        disk.fail_write=cut
                        r=self.request(2,data=b'new value',volume=volume)
                        if not r.status:r=self.request(11,volume=volume)
                        self.assertEqual(r.status,-5)
                        disk.fail_write=0;disk.crash()
                        result,recovered=self.mount(disk);self.assertEqual(result,1)
                        self.assertIn(self.read(volume=recovered),(b'old value',b'new value'))
                        self.assertFalse(disk.illegal)
    def test_failed_flush_recovers_complete_old_or_new_state(self):
        self.replace(b'old')
        for cut in (1,2):
            disk=Disk(self.disk.sectors);_,volume=self.mount(disk)
            self.assertEqual(self.request(10,volume=volume).status,0)
            self.assertEqual(self.request(2,data=b'NEW',volume=volume).status,0)
            disk.fail_flush=cut
            self.assertEqual(self.request(11,volume=volume).status,-5)
            disk.fail_flush=0;disk.crash()
            result,recovered=self.mount(disk);self.assertEqual(result,1)
            self.assertIn(self.read(volume=recovered),(b'old',b'NEW'))
    def test_corrupt_roots_with_marker_fail_closed(self):
        self.replace(b'first');self.replace(b'second')
        for bank in (0,1):
            root=bytearray(self.disk.root(bank));root[100]^=1;self.disk.replace_root(bank,root)
        result,_=self.mount(self.disk);self.assertEqual(result,-5)
    def test_corrupt_file_data_is_not_silently_returned(self):
        self.replace(b'check me')
        banks=[(struct.unpack_from('<Q',self.disk.root(i),8)[0],i) for i in (0,1)]
        _,bank=max(banks)
        start=struct.unpack_from('<I',self.disk.root(bank),64+20)[0]
        sector=bytearray(self.disk.sectors[start*8]);sector[0]^=1;self.disk.sectors[start*8]=bytes(sector)
        result,volume=self.mount(self.disk);self.assertEqual(result,1)
        self.assertEqual(self.request(3,size=32,volume=volume).status,-5)
    def test_valid_crc_does_not_allow_invalid_extent(self):
        self.replace(b'first')
        for bank in (0,1):
            root=bytearray(self.disk.root(bank))
            if not any(root):continue
            struct.pack_into('<I',root,64+20,1)
            struct.pack_into('<I',root,32,0);struct.pack_into('<I',root,32,zlib.crc32(root))
            self.disk.replace_root(bank,root)
        result,_=self.mount(self.disk);self.assertEqual(result,-5)
    def test_multi_file_commit_never_exposes_mixed_versions(self):
        self.replace(b'old-left','a')
        for i in range(29):self.assertEqual(self.request(1,f'empty{i}').status,0)
        self.replace(b'old-right','b') # Record is in a different root sector.
        initial=dict(self.disk.sectors)
        for write_through in (False,True):
            for cut in range(1,49):
                with self.subTest(cut=cut,write_through=write_through):
                    disk=Disk(initial,write_through);_,volume=self.mount(disk);disk.fail_write=cut;disk.torn=True
                    self.assertEqual(self.request(10,volume=volume).status,0)
                    result=self.request(2,'a',data=b'NEW-LEFT',volume=volume)
                    if not result.status:result=self.request(2,'b',data=b'NEW-RIGHT',volume=volume)
                    if not result.status:result=self.request(11,volume=volume)
                    self.assertEqual(result.status,-5)
                    disk.fail_write=0;disk.crash();status,recovered=self.mount(disk);self.assertEqual(status,1)
                    pair=(self.read('a',volume=recovered),self.read('b',volume=recovered))
                    self.assertIn(pair,((b'old-left',b'old-right'),(b'NEW-LEFT',b'NEW-RIGHT')))
    def legacy(self,stride=16408):
        data=bytearray(stride*8)
        for i,name,content in ((0,b'old.txt',b'preserved old data'),
                               (7,b'binary.bin',bytes(range(256))*3)):
            start=i*stride;data[start:start+len(name)]=name
            struct.pack_into('<II',data,start+16,1,len(content))
            data[start+24:start+24+len(content)]=content
        return data
    def test_import_v1_and_v2_and_preserve_legacy_sectors(self):
        for stride in (1048,16408):
            disk=Disk({i:bytes([i%256])*512 for i in range(581)})
            original=dict(disk.sectors);_,volume=self.mount(disk)
            payload=self.legacy(stride);buffer=c.create_string_buffer(bytes(payload))
            self.assertEqual(self.lib.fs3_import_legacy(volume,buffer,len(payload)),0)
            self.assertEqual({i:disk.sectors[i] for i in original},original)
            disk.crash();result,recovered=self.mount(disk);self.assertEqual(result,1)
            self.assertEqual(self.read('old.txt',volume=recovered),b'preserved old data')
            self.assertEqual(self.read('binary.bin',offset=250,volume=recovered),bytes(range(256))[250:]+bytes(range(26)))
            self.assertEqual(self.lib.fs3_import_legacy(recovered,buffer,len(payload)),-16)
    def test_invalid_import_never_writes_or_replaces_state(self):
        payload=self.legacy();payload[16]=2
        buffer=c.create_string_buffer(bytes(payload))
        self.assertEqual(self.lib.fs3_import_legacy(self.volume,buffer,len(payload)),-22)
        self.assertEqual(self.disk.writes,0)
    def test_interrupted_first_import_can_retry_without_stale_fallback(self):
        payload=self.legacy(1048);buffer=c.create_string_buffer(bytes(payload))
        for cut in (1,8,9,16,24,32):
            disk=Disk();_,volume=self.mount(disk);disk.fail_write=cut
            result=self.lib.fs3_import_legacy(volume,buffer,len(payload))
            self.assertEqual(result,-5)
            disk.fail_write=0;disk.crash();status,recovered=self.mount(disk)
            if status==0:
                self.assertEqual(self.lib.fs3_import_legacy(recovered,buffer,len(payload)),0)
            else:self.assertEqual(status,1)
            self.assertEqual(self.read('old.txt',volume=recovered),b'preserved old data')

if __name__=='__main__':unittest.main(verbosity=2)
