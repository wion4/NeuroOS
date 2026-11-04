"""Actual restricted .nos processes, original FS3 service and disposable disk."""
import json
import os
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from scoped_fixture import compile_scoped_test
from test_compiler import snapshot_files

folder=ROOT/('build/scoped-files-fs2' if os.environ.get('NEUROOS_FS_BACKEND')=='2' else 'build/scoped-files')
folder.mkdir(parents=True,exist_ok=True)
report=folder/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
disk=folder/'scoped-test-only.img'
os.environ.update(NEUROOS_DESKTOP='1',NEUROOS_AI_DEMO='0',NEUROOS_BOOTSTRAP_SDK='0',NEUROOS_DATA_DISK=str(disk))
target=neuro.build()
app=compile_scoped_test(neuro,target,folder,'tests/user_scoped_app.c','filedemo')
expected=seed_native_disk(disk,target,{'scope.nos':app,'victim.txt':b'victim','write.txt':b'seed'})
desktop=target/'esp/boot/desktop.nos';original=desktop.read_bytes()
try:
    desktop.write_bytes(compile_scoped_test(neuro,target,folder,'tests/user_scope_control.c','desktop'))
    with DesktopGuest(neuro,target,folder) as g:
        g.wait(b'DESKTOP EXIT CLEAN')
        serial=g.log.read_bytes();(folder/'serial.log').write_bytes(serial)
        assert b'SCOPED FILE TEST PASS' in serial,serial[-2500:]
        assert b'SCOPED APP FAIL' not in serial and b'SCOPE CONTROL FAIL' not in serial
finally:desktop.write_bytes(original)
assert snapshot_files(disk)==expected
report.write_text(json.dumps({'passed':True,'checks':[
    '188-byte bounded controller ABI and malformed/duplicate grants rejected',
    'immutable scope and argument snapshot', 'loader source access not inherited by app',
    'read-only, write-only existing C helpers, create-only and delete-only',
    'four files cannot combine authority across names', 'LIST/QUIT/kernel close denied',
    'legacy CREATE-before-WRITE uses nonmutating existence probe, no new target created',
    'probe response suppresses size/offset/data and preserves original opcode',
    'short or denied messages never enqueued', 'allowed mutations verified and restored',
    'private scoped draft rolled back on Stop', 'scoped journal flags',
    'all original files byte-identical and desktop PMM cleanup exact']},indent=2)+'\n')
print('PASS: ring-3 per-name operation grants, immutable scope, C helpers and rollback')
