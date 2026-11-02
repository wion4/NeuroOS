"""Build a deterministic static C library and flat header package for the guest."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

def build(root:Path,target:Path,clang,flags,tool):
    work=target/'sdk';work.mkdir(parents=True,exist_ok=True)
    sources=['kernel/runtime.c','user/api.c','user/file.c','user/heap.c','user/stdio.c',
             'user/format.c','user/string.c','user/stdlib.c','user/setjmp.S','user/strtod.c',
             'user/float_math.c','vendor/musl-1.2.5/floatscan.c']
    objects=[]
    for source in sources:
        obj=work/(Path(source).stem+'.o')
        extra=['-g0','-Os']
        if source=='user/strtod.c' or source.startswith('vendor/musl-'):
            extra+=['-Iports/musl/include','-Iuser/libc','-Dhidden=']
        if source.startswith('vendor/musl-'):extra+=['-Wno-parentheses','-Wno-sign-compare']
        subprocess.run([clang,*flags,*extra,'-c',source,'-o',str(obj)],cwd=root,check=True)
        objects.append(str(obj))
    archive=work/'libnos.a'
    archive.unlink(missing_ok=True) # Generated build artifact; never a user data file.
    ar=tool('LLVM_AR','llvm-ar',[r'C:\Program Files\LLVM\bin\llvm-ar.exe'])
    # Replacing all members by deterministic names keeps one member per module.
    subprocess.run([ar,'rcsD','--format=gnu',str(archive),*objects],check=True)
    files={'libnos.a':archive.read_bytes()}
    for name in ('stdio.h','string.h','errno.h','setjmp.h','fcntl.h','unistd.h'):
        files[name]=(root/'user/libc'/name).read_bytes()
    stdlib=(root/'user/libc/stdlib.h').read_text().replace('#include "../heap.h"',
        '#include <stddef.h>\nvoid *malloc(size_t);\nvoid *calloc(size_t,size_t);\nvoid *realloc(void *,size_t);\nvoid free(void *);')
    files['stdlib.h']=stdlib.encode()
    for name in ('neuro.h','stdint.h','limits.h','assert.h'):
        files[name]=(root/'user/sdk'/name).read_bytes()
    for name in ('stddef.h','stdarg.h','stdbool.h','stdnoreturn.h','stdalign.h','float.h'):
        files[name]=(root/'vendor/tinycc/include'/name).read_bytes()
    files['inttypes.h']=(root/'ports/tinycc/include/inttypes.h').read_bytes()
    files['COPYING.tcc']=(root/'vendor/tinycc/COPYING').read_bytes()
    files['COPYING.musl']=(root/'vendor/musl-1.2.5/COPYRIGHT').read_bytes()
    entries=bytearray();data=bytearray();offset=32+len(files)*32
    manifest=[]
    for name,content in sorted(files.items()):
        encoded=name.encode('ascii');assert len(encoded)<=15
        entries+=struct.pack('<16s4I',encoded,offset,len(content),zlib.crc32(content),0)
        data+=content;offset+=len(content)
        manifest.append({'name':name,'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest()})
    payload=entries+data
    header=struct.pack('<8sIIQII',b'NOSSDK01',len(files),1,32+len(payload),zlib.crc32(payload),0)
    (target/'esp/boot/sdk.pack').write_bytes(header+payload)
    (work/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return len(files)
