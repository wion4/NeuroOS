"""Run actual integer conversions/sort, compare conversions with the host CRT.

Host uses 32-bit long; the guest suite separately checks its 64-bit long ABI.
"""
import ctypes as c
from pathlib import Path
import random
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

class StdlibTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/stdlib-tests';target.mkdir(parents=True,exist_ok=True)
        support=target/'support.c'
        support.write_text('''#include "libc/stdio.h"
int errno, unexpected_output;
FILE *stderr;
int fprintf(FILE *stream,const char *format,...) {
    (void)stream;(void)format;++unexpected_output;return -1;
}
''')
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in (ROOT/'user/stdlib.c',support):
            obj=target/(source.stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding',
                            '-fno-builtin','-fno-stack-protector','-Iuser','-Wall','-Wextra','-Werror',
                            '-c',str(source),'-o',str(obj)],cwd=ROOT,check=True)
            objects.append(str(obj))
        names=('strtol','strtoul','strtoll','strtoull','qsort')
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
                        *('/export:'+name for name in names),'/export:errno,data',
                        '/export:unexpected_output,data','/out:'+str(target/'stdlib.dll')],check=True)
        cls.lib=c.CDLL(str(target/'stdlib.dll'));cls.crt=c.CDLL('ucrtbase')
        cls.crt._errno.restype=c.POINTER(c.c_int)
        cls.functions={}
        for own,other,ctype in (('strtol','strtol',c.c_long),('strtoul','strtoul',c.c_ulong),
                               ('strtoll','strtoll',c.c_longlong),('strtoull','strtoull',c.c_ulonglong)):
            a=getattr(cls.lib,own);b=getattr(cls.crt,other)
            for fn in (a,b):fn.argtypes=[c.c_void_p,c.POINTER(c.c_void_p),c.c_int];fn.restype=ctype
            cls.functions[own]=(a,b)
        cls.compare=c.CFUNCTYPE(c.c_int,c.c_void_p,c.c_void_p)
        cls.lib.qsort.argtypes=[c.c_void_p,c.c_size_t,c.c_size_t,cls.compare]

    def test_conversions_values_endptr_and_range(self):
        samples=(b'',b' ',b' +',b'no digits',b'0',b'09',b'0x',b'-0Xf!',b'  +42 tail',
                 b'-1',b'2147483647',b'2147483648',b'-2147483649',b'4294967296',
                 b'9223372036854775807',b'-9223372036854775808',b'9223372036854775808',
                 b'18446744073709551615',b'18446744073709551616suffix',
                 b'-99999999999999999999999999999',b'zzzz',b'101010101010')
        for name,(actual,reference) in self.functions.items():
            for text in samples:
                for base in (0,2,8,10,16,36):
                    with self.subTest(function=name,text=text,base=base):
                        buffer=c.create_string_buffer(text);end_a=c.c_void_p();end_b=c.c_void_p()
                        c.c_int.in_dll(self.lib,'errno').value=0;self.crt._errno()[0]=0
                        a=actual(buffer,c.byref(end_a),base);b=reference(buffer,c.byref(end_b),base)
                        if text==b'0x' and base in (0,16):
                            # The host CRT rejects the whole incomplete prefix.
                            # Our C parser consumes the valid leading zero; the
                            # guest suite independently checks this boundary.
                            self.assertEqual(a,0);self.assertEqual(end_a.value,c.addressof(buffer)+1)
                            self.assertEqual(c.c_int.in_dll(self.lib,'errno').value,0)
                            continue
                        self.assertEqual(a,b);self.assertEqual(end_a.value,end_b.value)
                        self.assertEqual(c.c_int.in_dll(self.lib,'errno').value,self.crt._errno()[0])
        self.assertEqual(c.c_int.in_dll(self.lib,'unexpected_output').value,0)

    def test_invalid_base_leaves_end_at_input(self):
        for base in (-1,1,37):
            buffer=c.create_string_buffer(b'123');end=c.c_void_p()
            self.assertEqual(self.lib.strtoll(buffer,c.byref(end),base),0)
            self.assertEqual(end.value,c.addressof(buffer))
            self.assertEqual(c.c_int.in_dll(self.lib,'errno').value,22)

    def test_sort_boundaries_and_random_inputs(self):
        @self.compare
        def compare(a,b):
            x=c.cast(a,c.POINTER(c.c_int))[0];y=c.cast(b,c.POINTER(c.c_int))[0]
            return (x>y)-(x<y)
        rng=random.Random(815)
        for count in (0,1,2,3,5,32,127):
            for _ in range(12):
                values=[rng.randint(-2**31,2**31-1) for _ in range(count)]
                array=(c.c_int*count)(*values)
                self.lib.qsort(array,count,c.sizeof(c.c_int),compare)
                self.assertEqual(list(array),sorted(values))

if __name__=='__main__':unittest.main(verbosity=2)
