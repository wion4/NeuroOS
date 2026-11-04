"""Actual pure kernel policy: exact names and operations, never real IO."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from test_services import Message

class Grant(c.Structure):
    _fields_=[('name',c.c_char*16),('rights',c.c_uint32)]
class Scope(c.Structure):
    _fields_=[('count',c.c_uint32),('reserved',c.c_uint32),('grants',Grant*4)]

def scope(*entries):
    out=Scope(count=len(entries))
    for i,(name,rights) in enumerate(entries):
        out.grants[i]=Grant(name=name.encode(),rights=rights)
    return out

class PolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/fs-policy-tests';target.mkdir(parents=True,exist_ok=True)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for src in ('kernel/fs_policy.c','kernel/runtime.c'):
            obj=target/(Path(src).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding',
                '-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror',
                '-c',str(ROOT/src),'-o',str(obj)],check=True)
            objects.append(str(obj))
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
            '/export:fs_scope_valid','/export:fs_scope_allows','/export:fs_loader_allows','/export:fs_scope_create_probe',
            '/out:'+str(target/'policy.dll')],check=True)
        cls.lib=c.CDLL(str(target/'policy.dll'))
        cls.lib.fs_scope_valid.argtypes=[c.POINTER(Scope)];cls.lib.fs_scope_valid.restype=c.c_bool
        cls.lib.fs_scope_allows.argtypes=[c.POINTER(Scope),c.POINTER(Message)]
        cls.lib.fs_scope_allows.restype=c.c_bool
        cls.lib.fs_scope_create_probe.argtypes=[c.POINTER(Scope),c.POINTER(Message)]
        cls.lib.fs_scope_create_probe.restype=c.c_bool
        cls.lib.fs_loader_allows.argtypes=[c.c_char_p,c.POINTER(Message)]
        cls.lib.fs_loader_allows.restype=c.c_bool
    def valid(self,s):return self.lib.fs_scope_valid(c.byref(s))
    def allowed(self,s,op,name='file.txt'):
        q=Message(op=op,name=name.encode(),size=7,offset=123)
        before=bytes(s),bytes(q)
        result=self.lib.fs_scope_allows(c.byref(s),c.byref(q))
        self.assertEqual(before,(bytes(s),bytes(q)))
        return result
    def test_wire_and_empty_or_invalid_policy(self):
        self.assertEqual((c.sizeof(Grant),c.sizeof(Scope)),(20,88))
        self.assertFalse(self.lib.fs_scope_valid(None))
        self.assertFalse(self.lib.fs_scope_allows(None,None))
        for count in (0,5,0xffffffff):
            s=scope(('file.txt',15));s.count=count
            self.assertFalse(self.valid(s));self.assertFalse(self.allowed(s,3))
        s=scope(('file.txt',15));s.reserved=1;self.assertFalse(self.valid(s))
        for rights in (0,16,0xffffffff):
            s=scope(('file.txt',rights));self.assertFalse(self.valid(s))
        s=scope(('file.txt',1));s.grants[3].rights=1;self.assertFalse(self.valid(s))
        s=scope(('file.txt',1));s.grants[1].name=b'unused';self.assertFalse(self.valid(s))
    def test_canonical_names_and_case_sensitive_duplicates(self):
        for name in ('a','A.9_-','123456789012345'):
            self.assertTrue(self.valid(scope((name,1))))
        for name in ('','.','..','../file','/file','a b','a:b','a\\b','1234567890123456'):
            self.assertFalse(self.valid(scope((name,1))),name)
        s=scope(('file',1));c.memmove(c.addressof(s.grants[0]),b'file\0hidden',11)
        self.assertFalse(self.valid(s))
        s=scope(('file',1));c.memmove(c.addressof(s.grants[0]),b'\xff\0',2)
        self.assertFalse(self.valid(s))
        self.assertFalse(self.valid(scope(('file',1),('file',2))))
        self.assertTrue(self.valid(scope(('file',1),('File',2))))
    def test_every_right_combination_is_bound_to_its_name(self):
        for rights in range(1,16):
            s=scope(('file.txt',rights),('other',15))
            for op,need in ((1,4),(2,2),(3,1),(4,8),(6,1),(8,2)):
                self.assertEqual(self.allowed(s,op),bool(rights&need),(rights,op))
                self.assertFalse(self.allowed(s,op,'foreign'))
                self.assertFalse(self.allowed(s,op,'File.txt'))
                self.assertTrue(self.allowed(s,op,'other'))
    def test_read_snapshot_cannot_commit_or_mutate(self):
        s=scope(('file.txt',1))
        for op in (10,12):self.assertTrue(self.allowed(s,op,'ignored'))
        for op in (1,2,4,5,7,8,9,11,13,0,14,0xffffffff):
            self.assertFalse(self.allowed(s,op),op)
        for rights in (2,4,8):
            self.assertTrue(self.allowed(scope(('file.txt',rights)),11,'ignored'))
    def test_two_files_do_not_combine_authority(self):
        s=scope(('file.txt',1),('writable',2),('new',4),('remove',8))
        self.assertTrue(self.valid(s))
        for name,op in (('file.txt',2),('writable',3),('new',2),('remove',1)):
            self.assertFalse(self.allowed(s,op,name))
    def test_request_hidden_suffix_and_nonterminated_name_denied(self):
        s=scope(('file.txt',15));q=Message(op=3,name=b'file.txt')
        c.memmove(c.addressof(q)+Message.name.offset,b'file.txt\0hidden',15)
        self.assertFalse(self.lib.fs_scope_allows(c.byref(s),c.byref(q)))
        c.memmove(c.addressof(q)+Message.name.offset,b'x'*16,16)
        self.assertFalse(self.lib.fs_scope_allows(c.byref(s),c.byref(q)))
    def test_private_loader_remains_read_only_for_exact_source(self):
        name=c.create_string_buffer(b'file.txt',16)
        for op in range(15):
            q=Message(op=op,name=b'file.txt')
            self.assertEqual(self.lib.fs_loader_allows(name,c.byref(q)),op in (3,6,10,12))
            q.name=b'foreign'
            self.assertEqual(self.lib.fs_loader_allows(name,c.byref(q)),op in (10,12))
        self.assertFalse(self.lib.fs_loader_allows(None,None))
    def test_legacy_create_probe_needs_write_and_never_expands_names(self):
        for rights in range(1,16):
            s=scope(('file.txt',rights));q=Message(op=1,name=b'file.txt')
            self.assertEqual(self.lib.fs_scope_create_probe(c.byref(s),c.byref(q)),
                             bool(rights&2) and not bool(rights&4))
            q.name=b'foreign'
            self.assertFalse(self.lib.fs_scope_create_probe(c.byref(s),c.byref(q)))
            q.name=b'file.txt';q.op=3
            self.assertFalse(self.lib.fs_scope_create_probe(c.byref(s),c.byref(q)))
        self.assertFalse(self.lib.fs_scope_create_probe(None,None))

if __name__=='__main__':unittest.main(verbosity=2)
