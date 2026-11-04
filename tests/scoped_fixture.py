"""Build test-only ordinary app/controller without installing anything on host."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def compile_scoped_test(neuro,target,folder,source,role):
    clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
    linker=neuro.tool('LD_LLD','ld.lld',[r'C:\Program Files\LLVM\bin\ld.lld.exe'])
    obj=folder/(role+'.o')
    subprocess.run([clang,'--target=x86_64-unknown-none-elf','-std=c11','-O2',
        '-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-mno-red-zone',
        '-mno-avx','-fno-builtin','-Wall','-Wextra','-Werror',
        '-c',str(ROOT/source),'-o',str(obj)],check=True)
    binary=folder/(role+'.nos')
    subprocess.run([linker,'-nostdlib','-static','-s','-z','max-page-size=16','--build-id=none',
        '-T',str(ROOT/'user/linker.ld'),str(target/f'{role}-start.o'),
        str(target/f'{role}-runtime.o'),str(target/f'{role}-file.o'),str(obj),'-o',str(binary)],check=True)
    content=bytearray(binary.read_bytes());content[7]=0x4e;content[8]=1
    return bytes(content)
