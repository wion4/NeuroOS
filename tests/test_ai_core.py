"""Pinned real weights, upstream neural blocks, bounded private math and host oracle."""
import ctypes as c
import importlib.util
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
class AITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        neuro.verify_vendor()
        assets=json.loads((ROOT/'models.lock.json').read_text())['stories260k']['files']
        for name,record in assets.items():
            raw=(ROOT/'tools/models/stories260k'/name).read_bytes()
            if len(raw)!=record['bytes'] or hashlib.sha256(raw).hexdigest()!=record['sha256']:
                raise RuntimeError('Pinned model asset changed: '+name)
        target=ROOT/'build/ai-host';target.mkdir(parents=True,exist_ok=True)
        spec=importlib.util.spec_from_file_location('ai_prepare',ROOT/'ports/llama2c/prepare.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.prepare(ROOT,target)
        weights=(ROOT/'tools/models/stories260k/stories260K.bin').read_bytes()
        tokens=(ROOT/'tools/models/stories260k/tok512.bin').read_bytes()
        cls.pack=struct.pack('<8s6I',b'NOSAI001',len(weights),len(tokens),zlib.crc32(weights),zlib.crc32(tokens),0,0)+weights+tokens
        (target/'ai.pack').write_bytes(cls.pack)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        dlltool=neuro.tool('LLVM_DLLTOOL','llvm-dlltool',[r'C:\Program Files\LLVM\bin\llvm-dlltool.exe'])
        exports=['malloc','calloc','free','qsort','exit','sqrtf','expf','powf','sinf','cosf']
        definition=target/'crt.def';definition.write_text('LIBRARY ucrtbase.dll\nEXPORTS\n'+'\n'.join(exports)+'\n')
        subprocess.run([dlltool,'-m','i386:x86-64','-d',str(definition),'-l',str(target/'crt.lib')],check=True)
        shim=target/'shim.c';shim.write_text('''#include "libc/stdio.h"
int errno,_fltused; FILE *stdout,*stderr;
size_t fwrite(const void *p,size_t size,size_t count,FILE *f){(void)p;(void)size;(void)f;return count;}
''')
        cls.libraries=[]
        for oracle in (False,True):
            objects=[]
            for source in ['user/ai_core.c','ports/llama2c/math.c','user/format.c','user/string.c','kernel/runtime.c',str(shim)]:
                obj=target/(('oracle-' if oracle else 'native-')+Path(source).stem+'.obj')
                subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unknown-pragmas','-Iuser','-I'+str(target),*(['-mlong-double-80'] if source=='ports/llama2c/math.c' else []),*(['-DNEUROOS_AI_ORACLE=1'] if oracle else []),'-c',source,'-o',str(obj)],cwd=ROOT,check=True)
                objects.append(str(obj))
            output=target/('oracle.dll' if oracle else 'native.dll')
            names=['ai_core_init','ai_core_generate','ai_core_release','ai_expf','ai_sqrtf','ai_powf','ai_sinf','ai_cosf']
            subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,str(target/'crt.lib'),*['/export:'+n for n in names],'/out:'+str(output)],check=True)
            lib=c.CDLL(str(output));lib.ai_core_init.argtypes=[c.c_void_p,c.c_uint];lib.ai_core_init.restype=c.c_int
            lib.ai_core_generate.argtypes=[c.c_char_p,c.c_uint,c.c_void_p,c.c_uint,c.c_void_p,c.POINTER(c.c_uint)];lib.ai_core_generate.restype=c.c_int
            cls.libraries.append(lib)
        cls.crt=c.CDLL('ucrtbase')
    def setUp(self):
        self.buffers=[]
        for lib in self.libraries:
            buf=c.create_string_buffer(self.pack);self.buffers.append(buf)
            self.assertEqual(lib.ai_core_init(buf,len(self.pack)),0)
    def tearDown(self):
        for lib in self.libraries:lib.ai_core_release()
    def generate(self,lib,prompt,steps):
        output=c.create_string_buffer(1536);trace=(c.c_uint*96)();count=c.c_uint()
        n=lib.ai_core_generate(prompt.encode(),steps,output,len(output),trace,c.byref(count))
        self.assertGreaterEqual(n,0);self.assertLessEqual(count.value,steps)
        return output.raw[:n],list(trace[:count.value])
    def test_real_weights_match_native_crt_math_oracle(self):
        report=[]
        for prompt in ('','Once upon a time','The cat'):
            actual=self.generate(self.libraries[0],prompt,48)
            reference=self.generate(self.libraries[1],prompt,48)
            self.assertEqual(actual,reference)
            self.assertEqual(actual,self.generate(self.libraries[0],prompt,48))
            report.append({'prompt':prompt,'text':actual[0].decode(),'trace':actual[1]})
        self.assertTrue(report[0]['text'].startswith('Once upon a time, there was a little girl named Lily.'))
        (ROOT/'build/ai-host/oracle-result.json').write_text(json.dumps({'passed':True,'cases':report},indent=2)+'\n')
    def test_math_matches_host_in_domain(self):
        rng=random.Random(2617);lib=self.libraries[0]
        for name,values in [('expf',[-104,-100,-80,0,1,80,88]+[rng.uniform(-100,88) for _ in range(500)]),('sqrtf',[0,1,1e-5,10000]+[rng.uniform(0,10000) for _ in range(500)]),('sinf',[rng.uniform(-512,512) for _ in range(500)]),('cosf',[rng.uniform(-512,512) for _ in range(500)])]:
            actual=getattr(lib,'ai_'+name);reference=getattr(self.crt,name)
            actual.argtypes=reference.argtypes=[c.c_float];actual.restype=reference.restype=c.c_float
            for x in values:
                a,b=actual(x),reference(x);self.assertLessEqual(abs(a-b),max(abs(b)*2e-6,2e-45),(name,x,a,b))
        actual=lib.ai_powf;reference=self.crt.powf
        actual.argtypes=reference.argtypes=[c.c_float,c.c_float];actual.restype=reference.restype=c.c_float
        for i in range(100):self.assertAlmostEqual(actual(10000,i/100)/reference(10000,i/100),1,places=6)
    def test_bad_headers_crc_dimensions_and_nan_fail_closed(self):
        for change,expected in [('magic',-22),('crc',-5),('shape',-22),('nan',-22),('token',-22)]:
            data=bytearray(self.pack)
            if change=='magic':data[0]^=1
            if change=='crc':data[100]^=1
            if change=='shape':struct.pack_into('<I',data,32,0)
            if change=='nan':struct.pack_into('<I',data,60,0x7fc00000)
            if change=='token':struct.pack_into('<I',data,32+1056540,0)
            if change in ('shape','nan'):struct.pack_into('<I',data,16,zlib.crc32(data[32:32+1056540]))
            if change=='token':struct.pack_into('<I',data,20,zlib.crc32(data[32+1056540:]))
            buf=c.create_string_buffer(bytes(data));self.assertEqual(self.libraries[0].ai_core_init(buf,len(data)),expected,change)
    def test_request_limits_and_output_bounds(self):
        lib=self.libraries[0];trace=(c.c_uint*96)();count=c.c_uint();out=c.create_string_buffer(b'!'*40)
        for prompt,steps in ((b'x'*56,48),(b'\xff',48),(b'hello',0),(b'hello',97)):
            self.assertEqual(lib.ai_core_generate(prompt,steps,c.addressof(out)+1,8,trace,c.byref(count)),-22)
        self.assertEqual(lib.ai_core_generate(b'',48,c.addressof(out)+1,8,trace,c.byref(count)),-28)
        self.assertEqual(out.raw[0:1],b'!');self.assertEqual(out.raw[9:],b'!'*31+b'\0')
if __name__=='__main__':unittest.main()
