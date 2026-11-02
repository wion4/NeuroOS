"""Execute the production ELF validator and userspace RAMFS implementation on the host."""
import ctypes as c
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

class Segment(c.Structure):
    _fields_=[(n,c.c_uint64) for n in ('address','offset','filesz','memsz','flags')]
class Image(c.Structure):
    _fields_=[('entry',c.c_uint64),('count',c.c_uint64),('segments',Segment*16)]
class Message(c.Structure):
    _fields_=[('op',c.c_uint32),('offset',c.c_uint32),('name',c.c_char*16),
              ('size',c.c_uint32),('status',c.c_int32),('data',c.c_char*32)]

def elf(programs=None,entry=0x400000,kind=2,machine=62):
    if programs is None:programs=[(1,5,4096,0x400000,0,1,4096,4096)]
    ident=b'\x7fELF'+bytes([2,1,1,0x4e,1])+bytes(7)
    header=struct.pack('<16sHHIQQQIHHHHHH',ident,kind,machine,1,entry,64,0,0,64,56,len(programs),0,0,0)
    data=bytearray(header+b''.join(struct.pack('<IIQQQQQQ',*p) for p in programs))
    data+=bytes(max(0,8192-len(data)))
    return data

class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/service-host-tests';target.mkdir(parents=True,exist_ok=True)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in ('kernel/elf.c','user/fs.c','kernel/runtime.c'):
            obj=target/(Path(source).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-ffreestanding',
                '-fno-stack-protector','-fno-builtin','-Wall','-Wextra','-Werror','-Iuser',
                '-c',source,'-o',str(obj)],cwd=ROOT,check=True)
            objects.append(str(obj))
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
            '/export:elf_validate','/export:fs_handle','/export:fs_reset','/export:fs_export','/export:fs_import',
            '/out:'+str(target/'services.dll')],cwd=ROOT,check=True)
        cls.lib=c.CDLL(str(target/'services.dll'))
        cls.lib.elf_validate.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(Image)]
        cls.lib.elf_validate.restype=c.c_bool
        cls.lib.fs_handle.argtypes=[c.POINTER(Message),c.POINTER(Message)]
        cls.lib.fs_reset.argtypes=[]
        for name in ('fs_export','fs_import'):
            fn=getattr(cls.lib,name);fn.argtypes=[c.c_void_p,c.c_size_t];fn.restype=c.c_bool
    def setUp(self):self.lib.fs_reset()
    def valid(self,data):
        buf=c.create_string_buffer(bytes(data));image=Image()
        return self.lib.elf_validate(buf,len(data),c.byref(image)),image
    def request(self,op,name='note',offset=0,size=0,data=b''):
        q=Message(op=op,offset=offset,name=name.encode(),size=size,data=data);r=Message()
        self.lib.fs_handle(c.byref(q),c.byref(r));return r

    def test_elf_actual_built_programs(self):
        for app in ('ramfs','fsclient','filedemo','libcdemo','floatdemo','compiler'):
            valid,image=self.valid((ROOT/f'build/normal/esp/boot/{app}.nos').read_bytes())
            self.assertTrue(valid);self.assertGreater(image.count,0)
    def test_foreign_and_future_abi_rejected(self):
        for offset,value in ((7,0),(8,2)):
            data=elf();data[offset]=value;self.assertFalse(self.valid(data)[0])
    def test_elf_bss_and_readonly_data(self):
        good,image=self.valid(elf([(1,5,4096,0x400000,0,1,100,4096),
                                  (1,6,8192,0x401000,0,0,8192,4096)]))
        self.assertTrue(good);self.assertEqual(image.count,2)
    def test_elf_truncation_magic_machine_type(self):
        for data in (b'',elf()[:63],b'x'*8192,elf(machine=3),elf(kind=3)):
            self.assertFalse(self.valid(data)[0])
    def test_elf_program_table_bounds(self):
        for field,value,fmt in ((32,2**64-1,'Q'),(54,55,'H'),(56,17,'H'),(56,0,'H')):
            data=elf();struct.pack_into('<'+fmt,data,field,value)
            self.assertFalse(self.valid(data)[0])
    def test_elf_segment_file_and_virtual_bounds(self):
        cases=[(4096,0xffffffff80000000,1,4096),(4096,0x400001,1,4096),
               (8192,0x400000,1,4096),(4096,0x400000,4097,4096),
               (4096,0x6ff000,1,8192),(2**64-1,0x400000,1,4096)]
        for offset,va,fs,ms in cases:
            self.assertFalse(self.valid(elf([(1,5,offset,va,0,fs,ms,4096)]))[0])
    def test_elf_permissions_entry_and_alignment(self):
        for flags in (0,1,2,7,12):
            self.assertFalse(self.valid(elf([(1,flags,4096,0x400000,0,1,4096,4096)]))[0])
        self.assertFalse(self.valid(elf(entry=0x401000))[0])
        self.assertFalse(self.valid(elf([(1,5,4096,0x400000,0,1,4096,3)]))[0])
    def test_elf_overlapping_pages_and_dynamic(self):
        self.assertFalse(self.valid(elf([(1,5,4096,0x400000,0,1,4096,4096),
                                        (1,6,4096,0x400000,0,1,4096,4096)]))[0])
        for kind in (2,3,7):
            self.assertFalse(self.valid(elf([(kind,5,4096,0x400000,0,1,4096,4096)]))[0])
    def test_elf_resource_cap_and_null(self):
        self.assertFalse(self.valid(elf([(1,5,4096,0x400000,0,1,161*4096,4096)]))[0])
        image=Image(entry=99,count=99)
        self.assertFalse(self.lib.elf_validate(None,8192,c.byref(image)))
        self.assertEqual((image.entry,image.count),(0,0))

    def test_fs_create_write_read_delete(self):
        self.assertEqual(self.request(1).status,0)
        self.assertEqual(self.request(2,size=5,data=b'hello').status,0)
        r=self.request(3,size=32);self.assertEqual((r.status,r.size,r.data),(0,5,b'hello'))
        self.assertEqual(self.request(6).size,5)
        self.assertEqual(self.request(4).status,0)
        self.assertEqual(self.request(3,size=32).status,-2)
    def test_fs_duplicate_full_and_reuse(self):
        for i in range(8):self.assertEqual(self.request(1,f'f{i}').status,0)
        self.assertEqual(self.request(1,'f0').status,-17)
        self.assertEqual(self.request(1,'extra').status,-28)
        self.assertEqual(self.request(4,'f3').status,0)
        self.assertEqual(self.request(1,'extra').status,0)
    def test_fs_invalid_names_and_requests(self):
        for name in ('','../note','a/b','.secret','a'*16):self.assertEqual(self.request(1,name).status,-22)
        self.assertEqual(self.request(99).status,-22)
        self.assertEqual(self.request(1,size=33).status,-22)
    def test_fs_limits_and_atomic_failure(self):
        self.request(1)
        self.assertEqual(self.request(2,offset=16380,size=4,data=b'last').status,0)
        self.assertEqual(self.request(2,offset=16380,size=5,data=b'bad!!').status,-27)
        self.assertEqual(self.request(2,offset=2**32-1,size=1,data=b'x').status,-27)
        r=self.request(3,offset=16380,size=32);self.assertEqual((r.size,r.data),(4,b'last'))
    def test_fs_holes_zero_length_eof_truncate(self):
        self.request(1);self.request(2,offset=5,size=1,data=b'x')
        r=self.request(3,size=6)
        raw=c.string_at(c.addressof(r)+Message.data.offset,6)
        self.assertEqual(raw,b'\0'*5+b'x')
        self.assertEqual(self.request(3,offset=100,size=32).size,0)
        self.request(2,offset=100,size=0);self.assertEqual(self.request(6).size,6)
        self.request(8);self.assertEqual(self.request(6).size,0)
    def test_fs_listing_and_recreate_clears_data(self):
        self.request(1,'one');self.request(1,'two')
        r=self.request(5);self.assertEqual(r.name,b'one')
        r=self.request(5,offset=r.offset);self.assertEqual(r.name,b'two')
        self.assertEqual(self.request(5,offset=r.offset).status,-2)
        self.assertEqual(self.request(5,offset=2**32-1).status,-2)
        self.request(2,'one',size=6,data=b'secret');self.request(4,'one');self.request(1,'one')
        self.assertEqual(self.request(6,'one').size,0)
    def test_snapshot_roundtrip_and_invalid_atomicity(self):
        self.request(1);self.request(2,size=6,data=b'secret')
        image=c.create_string_buffer(131264)
        self.assertTrue(self.lib.fs_export(image,131264))
        self.lib.fs_reset();self.assertEqual(self.request(6).status,-2)
        self.assertTrue(self.lib.fs_import(image,131264))
        self.assertEqual(self.request(3,size=32).data,b'secret')
        damaged=bytearray(image.raw);damaged[16]=2
        invalid=c.create_string_buffer(bytes(damaged))
        self.assertFalse(self.lib.fs_import(invalid,131264))
        self.assertEqual(self.request(3,size=32).data,b'secret')
        damaged=bytearray(image.raw);damaged[20:24]=struct.pack('<I',16385)
        self.assertFalse(self.lib.fs_import(c.create_string_buffer(bytes(damaged)),131264))
        self.assertFalse(self.lib.fs_import(image,8383))
    def test_large_file_and_legacy_import(self):
        self.request(1)
        self.assertEqual(self.request(2,offset=16352,size=32,data=b'x'*32).status,0)
        self.assertEqual(self.request(6).size,16384)
        image=c.create_string_buffer(131264);self.assertTrue(self.lib.fs_export(image,131264))
        self.lib.fs_reset();self.assertTrue(self.lib.fs_import(image,131264))
        self.assertEqual(self.request(3,offset=16352,size=32).data,b'x'*32)
        legacy=bytearray(8384);legacy[:4]=b'old\0';legacy[16]=1
        struct.pack_into('<I',legacy,20,3);legacy[24:27]=b'abc'
        self.assertTrue(self.lib.fs_import(c.create_string_buffer(bytes(legacy)),8384))
        self.assertEqual(self.request(3,'old',size=32).data,b'abc')

if __name__=='__main__':unittest.main(verbosity=2)
