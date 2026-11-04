"""Image commit must coexist with an unrelated FS request already in flight."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from test_compiler import snapshot_files

folder = ROOT/'build/loader-busy'
folder.mkdir(parents=True,exist_ok=True)
report=folder/'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk=folder/'loader-busy-test-only.img'
os.environ.update(NEUROOS_FS_BACKEND='3',NEUROOS_DESKTOP='1',NEUROOS_AI_DEMO='0',
                  NEUROOS_BOOTSTRAP_SDK='0',NEUROOS_DATA_DISK=str(disk))
target=neuro.build()
expected=seed_native_disk(disk,target)
clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
linker=neuro.tool('LD_LLD','ld.lld',[r'C:\Program Files\LLVM\bin\ld.lld.exe'])
paths={name:target/f'esp/boot/{name}.nos' for name in ('ramfs','loader','desktop')}
original={name:p.read_bytes() for name,p in paths.items()}
sources={'ramfs':'tests/user_loader_busy_fs.c','loader':'tests/user_loader_probe.c',
         'desktop':'tests/user_loader_busy_control.c'}
try:
    for role,source in sources.items():
        obj=folder/(role+'.o')
        defines=['-DLOADER_DELAY_COMMIT=1'] if role=='loader' else []
        subprocess.run([clang,'--target=x86_64-unknown-none-elf','-std=c11','-O2',
            '-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-mno-red-zone',
            '-mno-avx','-fno-builtin','-Wall','-Wextra','-Werror',*defines,
            '-c',str(ROOT/source),'-o',str(obj)],check=True)
        parts=['start','runtime']
        if role=='ramfs': parts+=['fs','storage','fs3']
        if role=='loader': parts+=['heap']
        if role=='desktop': parts+=['file']
        binary=folder/(role+'.nos')
        subprocess.run([linker,'-nostdlib','-static','-z','max-page-size=4096','--build-id=none',
            '-T',str(ROOT/'user/linker.ld'),*[str(target/f'{role}-{x}.o') for x in parts],
            str(obj),'-o',str(binary)],check=True)
        content=bytearray(binary.read_bytes());content[7]=0x4e;content[8]=1
        paths[role].write_bytes(content)
    with DesktopGuest(neuro,target,folder) as g:
        g.wait(b'DESKTOP EXIT CLEAN')
        serial=g.log.read_bytes()
        (folder/'serial.log').write_bytes(serial)
        assert b'LOADER BUSY FS TEST PASS' in serial,serial[-1600:]
        assert b'LOADER PRIVILEGE TEST PASS' in serial
        assert serial.count(b'FS TEST UNRELATED REQUEST BUSY')>=1
        assert (serial.rfind(b'FS TEST UNRELATED REQUEST BUSY') <
                serial.index(b'LOADER PRIVILEGE TEST PASS') < serial.index(b'NATIVE JOB STARTED') <
                serial.rfind(b'FS TEST UNRELATED REQUEST DONE'))
        assert b'LOADER PRIVILEGE TEST FAIL' not in serial and b'LOADER BUSY FS TEST FAIL' not in serial
finally:
    for name,content in original.items(): paths[name].write_bytes(content)
assert snapshot_files(disk)==expected
report.write_text(json.dumps({'passed':True,'checks':[
    'real FS3 transaction and unchanged storage',
    'successful unrelated STAT delayed in test server across image commit',
    'production loader with test-only pre-commit sleep',
    'private completion succeeds without global FS idle requirement',
    'load error is not fabricated by another client activity',
    'no files changed and desktop PMM cleanup passes']},indent=2)+'\n')
print('PASS: native image commit succeeds while another FS client request is in flight')
