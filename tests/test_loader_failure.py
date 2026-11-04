"""Loader #PF, wall deadline and actual FS #PF during loading, on own disks."""
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from desktop_guest import DesktopGuest
from guest_debug import packet
from native_fixture import seed_native_disk
from test_compiler import snapshot_files

folder = ROOT / 'build/loader-failure'
folder.mkdir(parents=True,exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
os.environ.update(NEUROOS_FS_BACKEND='3',NEUROOS_DESKTOP='1',NEUROOS_AI_DEMO='0',NEUROOS_BOOTSTRAP_SDK='0')
target = neuro.build()
clang = neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
linker = neuro.tool('LD_LLD','ld.lld',[r'C:\Program Files\LLVM\bin\ld.lld.exe'])
nm = neuro.tool('LLVM_NM','llvm-nm',[r'C:\Program Files\LLVM\bin\llvm-nm.exe'])
symbols = subprocess.check_output([nm,str(target/'esp/boot/kernel.elf')],text=True)
address = int(re.search(r'^([0-9a-fA-F]+)\s+\w\s+fault_after_io$',symbols,re.MULTILINE)[1],16)
loader, desktop = target/'esp/boot/loader.nos',target/'esp/boot/desktop.nos'
original_loader, original_desktop = loader.read_bytes(),desktop.read_bytes()
original_qemu = neuro.qemu_command

def compile_test(source,role,defines):
    obj = folder / (role+'.o')
    subprocess.run([clang,'--target=x86_64-unknown-none-elf','-std=c11','-O2',
        '-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-mno-red-zone',
        '-mno-avx','-fno-builtin','-Wall','-Wextra','-Werror',*['-D'+x for x in defines],
        '-c',str(ROOT/source),'-o',str(obj)],check=True)
    binary = folder / (role+'.nos')
    objects = [target/(role+'-start.o'),target/(role+'-runtime.o')]
    if role=='desktop': objects.append(target/'desktop-file.o')
    subprocess.run([linker,'-nostdlib','-static','-z','max-page-size=4096','--build-id=none',
        '-T',str(ROOT/'user/linker.ld'),*[str(x) for x in objects],str(obj),'-o',str(binary)],check=True)
    b=bytearray(binary.read_bytes());b[7]=0x4e;b[8]=1
    return b

completed=[]
try:
    for mode,error,closed,recovered in (('fault',-14,1,0),('deadline',-110,1,0),('fs-fault',-32,0,1)):
        local=folder/mode;local.mkdir(exist_ok=True)
        disk=local/'loader-failure-test-only.img'
        os.environ['NEUROOS_DATA_DISK']=str(disk)
        expected=seed_native_disk(disk,target)
        desktop.write_bytes(compile_test('tests/user_loader_control.c','desktop',[
            f'EXPECTED_ERROR={error}',f'EXPECTED_CLOSE={closed}',f'EXPECTED_RECOVERY={recovered}']))
        if mode=='fs-fault':
            loader.write_bytes(original_loader)
            with socket.socket() as probe:
                probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
            neuro.qemu_command=lambda *a,**kw: original_qemu(*a,**kw)+['-gdb',f'tcp:127.0.0.1:{port}']
        else:
            neuro.qemu_command=original_qemu
            loader.write_bytes(compile_test('tests/user_loader_failure.c','loader',[
                f'LOADER_STALL={int(mode=="deadline")}']))
        with DesktopGuest(neuro,target,local) as g:
            if mode=='fs-fault':
                g.qmp('stop')
                with socket.create_connection(('127.0.0.1',port),timeout=3) as connection:
                    assert packet(connection,f'M{address:x},4:64000000')==b'OK'
                g.qmp('cont')
            g.wait(b'LOADER FAILURE TEST PASS',timeout=75)
            g.wait(b'DESKTOP EXIT CLEAN')
            serial=g.log.read_bytes()
            assert b'LOADER FAILURE TEST FAIL' not in serial and b'NATIVE JOB STARTED' not in serial
            if mode=='fault': assert b'TASK 4 FAULT 14 CONTAINED' in serial
            if mode=='fs-fault': assert b'TASK 0 FAULT 14 CONTAINED' in serial
            (local/'serial.log').write_bytes(serial)
        assert snapshot_files(disk)=={**expected,'after.txt':b'after'}
        completed.append(mode)
        report.write_text(json.dumps({'passed':False,'completed':completed},indent=2)+'\n')
        print('PASS: loading failure mode',mode,flush=True)
finally:
    loader.write_bytes(original_loader)
    desktop.write_bytes(original_desktop)
    neuro.qemu_command=original_qemu
report.write_text(json.dumps({'passed':True,'completed':completed,'checks':[
    'loader page fault rolls back its snapshot', '60-second wall deadline closes sleeping loader',
    'actual mid-PIO FS fault is not replayed', 'fresh FS mount preserves controller',
    'numeric load error and cleanup events, never Started', 'normal controller cannot exec image',
    'subsequent file write succeeds', 'all seed files unchanged and desktop page cleanup exact']},indent=2)+'\n')
print('PASS: loader fault/deadline and filesystem fault contained before executable starts')
