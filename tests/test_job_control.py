"""Actual ring-3 controller, ordinary children and two live FS clients; no model."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from test_compiler import snapshot_files

folder = ROOT / ('build/job-control-fs2' if os.environ.get('NEUROOS_FS_BACKEND') == '2'
                 else 'build/job-control')
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'control-test-only.img'
os.environ.update(NEUROOS_DESKTOP='1', NEUROOS_AI_DEMO='0', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
assert 'ai.pack' not in (target / 'esp/limine.conf').read_text()
expected = seed_native_disk(disk, target)
clang = neuro.tool('CLANG', 'clang', [r'C:\Program Files\LLVM\bin\clang.exe'])
linker = neuro.tool('LD_LLD', 'ld.lld', [r'C:\Program Files\LLVM\bin\ld.lld.exe'])
obj = folder / 'control.o'
subprocess.run([clang, '--target=x86_64-unknown-none-elf', '-std=c11', '-O2',
    '-ffreestanding', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mno-red-zone',
    '-mno-avx', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-c',
    str(ROOT / 'tests/user_job_control.c'), '-o', str(obj)], check=True)
binary = folder / 'control.nos'
subprocess.run([linker, '-nostdlib', '-static', '-z', 'max-page-size=4096',
    '--build-id=none', '-T', str(ROOT / 'user/linker.ld'), str(target / 'desktop-start.o'),
    str(target / 'desktop-runtime.o'), str(target / 'desktop-file.o'), str(obj),
    '-o', str(binary)], check=True)
data = bytearray(binary.read_bytes()); data[7] = 0x4e; data[8] = 1
desktop = target / 'esp/boot/desktop.nos'
original = desktop.read_bytes()
try:
    desktop.write_bytes(data)
    with DesktopGuest(neuro, target, folder) as guest:
        guest.wait(b'JOB CONTROL TEST PASS', timeout=60)
        guest.wait(b'JOB EVENT TEST PASS')
        guest.wait(b'DESKTOP EXIT CLEAN')
        guest.wait(b'SHELL READY')
        serial = guest.log.read_bytes()
        assert b'JOB CONTROL FAIL' not in serial and b'DESKTOP SESSION FAILED' not in serial
        (folder / 'serial.log').write_bytes(serial)
finally:
    desktop.write_bytes(original)
assert snapshot_files(disk) == expected
report.write_text(json.dumps({'passed': True, 'checks': [
    'real 128 MiB guest, no model or network', 'invalid requests and RX destinations',
    'single outstanding job and explicit reap', 'cancel and stale handle rejection',
    'finished result survives invalid poll', 'ordinary process cannot forge kernel control',
    'private transaction, foreign commit/abort denied', 'cancel sleeping process and roll back draft',
    'two simultaneous FS clients with distinct replies', 'all fixture files byte-identical',
    'kernel desktop resource check passes', 'read-only versioned event query',
    'event order, owner and completion values', 'charged CPU ticks and peak heap pages',
    'event journal overwrite detection and independent cursors']}, indent=2) + '\n')
print('PASS: native job handles, cancel/reap, private FS sessions, live replies and complete cleanup')
