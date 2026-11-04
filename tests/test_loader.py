"""Real GUI while a 1 MiB image is read by the restricted ring-3 loader."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_fs3_disk
from test_compiler import snapshot_files

folder = ROOT / 'build/loader-integration'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'loader-test-only.img'
os.environ.update(NEUROOS_FS_BACKEND='3', NEUROOS_DESKTOP='1',
                  NEUROOS_AI_DEMO='0', NEUROOS_BOOTSTRAP_SDK='0', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
hello = (target / 'esp/boot/hello.nos').read_bytes()
files = {'hello.nos': hello, 'big.nos': hello + bytes(1048576 - len(hello)),
         'sentinel.txt': b'original'}
seed_fs3_disk(disk, files)
assert snapshot_files(disk) == files
clang = neuro.tool('CLANG', 'clang', [r'C:\Program Files\LLVM\bin\clang.exe'])
linker = neuro.tool('LD_LLD', 'ld.lld', [r'C:\Program Files\LLVM\bin\ld.lld.exe'])
obj = folder / 'probe.o'
subprocess.run([clang, '--target=x86_64-unknown-none-elf', '-std=c11', '-O2',
    '-ffreestanding','-fno-stack-protector','-fno-pic','-fno-pie','-mno-red-zone',
    '-mno-avx','-fno-builtin','-Wall','-Wextra','-Werror',
    '-c',str(ROOT / 'tests/user_loader_probe.c'),'-o',str(obj)], check=True)
probe = folder / 'probe.nos'
subprocess.run([linker,'-nostdlib','-static','-z','max-page-size=4096','--build-id=none',
    '-T',str(ROOT / 'user/linker.ld'),str(target / 'loader-start.o'),
    str(target / 'loader-runtime.o'),str(target / 'loader-heap.o'),str(obj),'-o',str(probe)],check=True)
content = bytearray(probe.read_bytes()); content[7] = 0x4e; content[8] = 1
loader = target / 'esp/boot/loader.nos'
original_loader = loader.read_bytes()

def pages(data):
    return int(re.search(rb'PAGES FREE (\d+)', data)[1])

try:
    loader.write_bytes(content)
    with DesktopGuest(neuro, target, folder) as g:
        g.click(730,564); g.click(480,306); g.wait(b'SHELL READY')
        baseline = pages(g.shell('mem',b'HEAP FREE BYTES'))
        start = g.log.stat().st_size
        g.type('desktop\n'); g.wait(b'DESKTOP READY',start); time.sleep(.4)
        g.click(270,562); g.click(565,151); g.click(480,306)
        g.type('Loading ')
        g.click(598,562); g.click(255,148)
        for _ in range(15): g.key('backspace')
        g.type('big.nos')
        start = g.log.stat().st_size
        g.click(430,148); g.wait(b'NATIVE JOB LOADING',start)
        g.screenshot('asynchronous-loading')
        g.click(270,562); g.type('edit'); g.key('s','ctrl')
        overlap = g.wait(b'DESKTOP NOTE SAVE BUSY',start)
        assert b'NATIVE JOB STARTED' not in overlap, overlap[-1000:]
        g.screenshot('editing-during-loading')
        g.click(598,562); g.click(430,148)
        cancelled = g.wait(b'DESKTOP PROGRAM RESULT',start)
        assert b'NATIVE JOB CANCELLED' in cancelled and b'NATIVE JOB STARTED' not in cancelled
        assert b'NATIVE JOB FS SESSION CLOSED' in cancelled
        g.click(270,562); start = g.log.stat().st_size
        g.key('s','ctrl'); g.wait(b'DESKTOP NOTE SAVED',start)
        g.click(598,562)
        start = g.log.stat().st_size
        began = time.monotonic()
        g.click(430,148)
        complete = g.wait(b'DESKTOP PROGRAM RESULT',start,75)
        elapsed = time.monotonic() - began
        assert b'LOADER PRIVILEGE TEST PASS' in complete and b'EXIT CODE 42' in complete
        assert b'LOADER PRIVILEGE TEST FAIL' not in complete
        g.screenshot('large-image-loaded')
        # Close the owner before its next image is staged, including flags=0.
        start = g.log.stat().st_size
        g.click(430,148); g.wait(b'NATIVE JOB LOADING',start)
        g.key('q','ctrl','alt'); g.wait(b'DESKTOP EXIT CLEAN',start); g.wait(b'\r\n> ',start)
        assert b'NATIVE JOB STARTED' not in g.log.read_bytes()[start:]
        assert pages(g.shell('mem',b'HEAP FREE BYTES')) == baseline
        serial = g.log.read_bytes()
        (folder / 'serial.log').write_bytes(serial)
finally:
    loader.write_bytes(original_loader)

after = snapshot_files(disk)
assert after == {**files, 'note001.txt': b'Loading edit'}
report.write_text(json.dumps({'passed':True,'image_bytes':len(files['big.nos']),
    'load_seconds':elapsed,'checks':[
    'real GUI accepts text while private loader reads 1 MiB',
    'read snapshot returns busy without losing editor text',
    'Stop during loading, session rollback and retry Save',
    'full 1 MiB read and kernel ELF validation before execution',
    'private loader cannot write, access other names, control jobs or use ATA/display',
    'malformed completion, kernel/cross-page buffers and invalid ELF rejected',
    'owner exit closes active loader session even without child file grants',
    'exact PMM baseline and disk contents preserved']},indent=2)+'\n')
print('PASS: responsive 1 MiB loading, Stop, read snapshot, role boundaries and page cleanup')
