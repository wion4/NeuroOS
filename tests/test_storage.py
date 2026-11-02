"""Exercise production ATA polling against delayed/failed command phases."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

class StorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/storage-tests';target.mkdir(parents=True,exist_ok=True)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in ('tests/storage_harness.c','user/fs.c','kernel/runtime.c'):
            obj=target/(Path(source).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-DNEUROOS_HOST_ABI',
                            '-ffreestanding','-fno-builtin','-fno-stack-protector',
                            '-Wall','-Wextra','-Werror','-c',source,'-o',str(obj)],cwd=ROOT,check=True)
            objects.append(str(obj))
        exports=('ata_reset','ata_violations','ata_completed_flushes','ata_test_read',
                 'ata_test_write','ata_test_flush','ata_test_ready','storage_load','storage_save')
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
                        *('/export:'+name for name in exports),'/out:'+str(target/'storage.dll')],check=True)
        cls.lib=c.CDLL(str(target/'storage.dll'))
        cls.lib.ata_reset.argtypes=[c.c_uint]
        for name in ('ata_test_read','ata_test_write'):
            fn=getattr(cls.lib,name);fn.argtypes=[c.c_uint,c.c_void_p];fn.restype=c.c_bool
        cls.lib.ata_test_ready.argtypes=[c.c_bool];cls.lib.ata_test_ready.restype=c.c_bool
        cls.lib.ata_test_flush.restype=c.c_bool
        cls.lib.storage_save.restype=c.c_bool
    def test_drq_must_clear_before_next_command(self):
        self.lib.ata_reset(0)
        data=(c.c_uint16*256)(*(i^0x5a5a for i in range(256)))
        out=(c.c_uint16*256)()
        for lba in (64,65,66):
            self.assertTrue(self.lib.ata_test_write(lba,data))
            self.assertTrue(self.lib.ata_test_flush())
            self.assertTrue(self.lib.ata_test_read(lba,out))
            self.assertEqual(bytes(data),bytes(out))
        self.assertEqual(self.lib.ata_violations(),0)
        self.assertEqual(self.lib.ata_completed_flushes(),3)
    def test_flush_waits_past_command_latency_until_completed(self):
        self.lib.ata_reset(0)
        self.assertTrue(self.lib.ata_test_flush())
        self.assertEqual(self.lib.ata_completed_flushes(),1)
    def test_errors_missing_and_timeout_fail(self):
        for mode in (1,2,3):
            for data in (False,True):
                self.lib.ata_reset(mode)
                self.assertFalse(self.lib.ata_test_ready(data))
    def test_full_snapshot_sequence(self):
        self.lib.ata_reset(0)
        self.assertTrue(self.lib.storage_save())
        self.assertEqual(self.lib.ata_violations(),0)
        self.assertEqual(self.lib.ata_completed_flushes(),4)

if __name__=='__main__':unittest.main(verbosity=2)
