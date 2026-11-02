"""Compare the real diagnostic formatter with the host C runtime.

Guest tests cover the LP64 ABI and files; this checks formatting/truncation
independently under the host ABI. No source implementation is duplicated here.
"""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

class FormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/format-tests';target.mkdir(parents=True,exist_ok=True)
        (target/'error.c').write_text('''#include "libc/stdio.h"
int errno, host_unsupported;
FILE *stdout;
void *malloc(size_t bytes) { (void)bytes; ++host_unsupported; return 0; }
void free(void *p) { (void)p; ++host_unsupported; }
size_t fwrite(const void *p,size_t size,size_t count,FILE *s) {
    (void)p;(void)size;(void)count;(void)s; ++host_unsupported; return 0;
}
''')
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in (ROOT/'user/format.c',ROOT/'kernel/runtime.c',target/'error.c'):
            obj=target/(source.stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding',
                '-ffunction-sections','-fdata-sections','-fno-stack-protector','-fno-builtin',
                '-Wall','-Wextra','-Werror','-Iuser','-c',str(source),'-o',str(obj)],cwd=ROOT,check=True)
            objects.append(str(obj))
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib','/opt:ref',*objects,
                        '/export:snprintf','/export:host_unsupported,data',
                        '/out:'+str(target/'format.dll')],cwd=ROOT,check=True)
        cls.library=c.CDLL(str(target/'format.dll'))
        cls.actual=cls.library.snprintf
        cls.actual.argtypes=[c.c_void_p,c.c_size_t,c.c_char_p];cls.actual.restype=c.c_int
        cls.runtime=c.CDLL('msvcrt')
        cls.reference=cls.runtime._snprintf
        cls.reference.argtypes=[c.c_void_p,c.c_size_t,c.c_char_p];cls.reference.restype=c.c_int

    def compare(self,fmt,*values):
        full=c.create_string_buffer(4096)
        n=self.reference(full,len(full),fmt,*values)
        self.assertGreaterEqual(n,0)
        expected=full.raw[:n]
        for capacity in (0,1,2,5,17,4096):
            with self.subTest(format=fmt,capacity=capacity,expected=expected):
                storage=c.create_string_buffer(b'!'*4100)
                pointer=c.addressof(storage)+1
                result=self.actual(pointer,capacity,fmt,*values)
                self.assertEqual(result,n)
                self.assertEqual(storage.raw[0:1],b'!')
                if capacity:
                    self.assertEqual(storage.raw[1:1+min(n,capacity-1)],expected[:capacity-1])
                    self.assertEqual(storage.raw[1+min(n,capacity-1)],0)
                self.assertEqual(storage.raw[1+capacity:2+capacity],b'!')
        self.assertEqual(c.c_int.in_dll(self.library,'host_unsupported').value,0)

    def test_signed_flags_width_precision_and_limits(self):
        for value in (0,1,-1,42,-42,-2**31,2**31-1):
            for fmt in (b'%d',b'%+08d',b'%-12.5d',b'% 12d',b'%.0d',b'%0-12d'):
                self.compare(fmt,c.c_int(value))
        for value in (0,-2**63,2**63-1):
            self.compare(b'%+025lld',c.c_longlong(value))

    def test_unsigned_bases(self):
        for value in (0,1,8,42,2**32-1):
            for fmt in (b'%u',b'%#08x',b'%#012X',b'%#12o',b'%#.0o',b'%#.0x',b'%#.8o'):
                self.compare(fmt,c.c_uint(value))
        self.compare(b'%llu',c.c_ulonglong(2**64-1))

    def test_strings_stars_and_promoted_lengths(self):
        for fmt in (b'%s',b'%.3s',b'%10.3s',b'%-10s'):
            self.compare(fmt,c.c_char_p(b'abcdef'))
        self.compare(b'%*.*s',c.c_int(-12),c.c_int(4),c.c_char_p(b'abcdef'))
        self.compare(b'%*.*d',c.c_int(9),c.c_int(-1),c.c_int(-21))
        # The legacy host runtime ignores hh; the guest test checks it against
        # the C integer-promotion rules instead of this unsuitable oracle.
        self.compare(b'%hd %hu',c.c_int(-32768),c.c_int(65535))
        self.compare(b'a%cb %% %zu',c.c_int(0),c.c_size_t(70000))

if __name__=='__main__':unittest.main(verbosity=2)
