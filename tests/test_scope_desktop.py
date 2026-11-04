"""Real file permission editor, per-name grants, denied writes and Stop."""
import json
import os
from pathlib import Path
import re
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from scoped_fixture import compile_scoped_test
from test_compiler import snapshot_files

folder=ROOT/'build/scope-desktop';folder.mkdir(parents=True,exist_ok=True)
report=folder/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
disk=folder/'scope-desktop-test-only.img'
os.environ.update(NEUROOS_FS_BACKEND='3',NEUROOS_DESKTOP='1',NEUROOS_AI_DEMO='0',
    NEUROOS_BOOTSTRAP_SDK='0',NEUROOS_DATA_DISK=str(disk))
target=neuro.build()
app=compile_scoped_test(neuro,target,folder,'tests/user_scoped_app.c','filedemo')
expected=seed_native_disk(disk,target,{'scope.nos':app,'victim.txt':b'victim','write.txt':b'seed'})
def pages(data):return int(re.search(rb'PAGES FREE (\d+)',data)[1])

with DesktopGuest(neuro,target,folder) as g:
    g.click(730,564);g.click(480,306);g.wait(b'SHELL READY')
    baseline=pages(g.shell('mem',b'HEAP FREE BYTES'))
    start=g.log.stat().st_size;g.type('desktop\n');g.wait(b'DESKTOP READY',start);time.sleep(.4)
    g.click(270,562);g.click(565,151);g.click(480,306)
    note='Permissions keep this note.';g.type(note)
    g.click(598,562);g.click(255,148)
    for _ in range(15):g.key('backspace')
    g.type('scope.nos');g.click(300,187);g.type('read')
    g.click(565,103);g.click(220,188);g.type('sentinel.txt')
    g.click(220,237);g.type('sentinel.txt')
    start=g.log.stat().st_size;g.key('ret');g.wait(b'DESKTOP FILE SCOPE REJECTED',start)
    for _ in range(15):g.key('backspace')
    g.screenshot('read-only-file-permissions')
    start=g.log.stat().st_size;g.click(225,420);g.wait(b'DESKTOP FILE SCOPE APPLIED',start)
    start=g.log.stat().st_size;g.click(430,148)
    result=g.wait(b'DESKTOP PROGRAM RESULT',start)
    assert b'SCOPED APP PASS' in result and b'EXIT CODE 0' in result
    # The same scope does not gain WRITE when arguments change.
    g.click(300,187)
    for _ in range(4):g.key('backspace')
    g.type('attempt')
    start=g.log.stat().st_size;g.click(430,148)
    result=g.wait(b'DESKTOP PROGRAM RESULT',start)
    assert b'SCOPED WRITE DENIED PASS' in result and b'EXIT CODE 0' in result
    g.screenshot('write-denied-by-read-scope')
    g.click(565,103);g.click(445,188);g.click(511,188)
    g.screenshot('write-only-file-permissions')
    start=g.log.stat().st_size;g.click(225,420);g.wait(b'DESKTOP FILE SCOPE APPLIED',start)
    g.click(300,187)
    for _ in range(7):g.key('backspace')
    g.type('write')
    start=g.log.stat().st_size;g.click(430,148)
    result=g.wait(b'DESKTOP PROGRAM RESULT',start)
    assert b'SCOPED APP PASS' in result and b'EXIT CODE 0' in result
    g.click(665,103);g.wait(b'DESKTOP EVENTS UPDATED');time.sleep(.3)
    g.screenshot('scoped-job-events');g.key('esc')
    # Whole-job cancellation also closes a draft made within an allowed name.
    g.click(300,187)
    for _ in range(5):g.key('backspace')
    g.type('hold');g.click(565,103);g.click(220,188)
    for _ in range(15):g.key('backspace')
    g.type('allowed.txt')
    for x in (445,577,643):g.click(x,188)
    start=g.log.stat().st_size;g.click(225,420);g.wait(b'DESKTOP FILE SCOPE APPLIED',start)
    start=g.log.stat().st_size;g.click(430,148);g.wait(b'SCOPED PRIVATE DRAFT HELD',start)
    g.click(565,103);g.wait(b'DESKTOP JOB SETTINGS LOCKED',start)
    g.click(585,148);g.screenshot('active-file-scope-is-fixed')
    g.click(430,148);g.wait(b'DESKTOP PROGRAM RESULT',start)
    assert b'NATIVE JOB FS SESSION CLOSED' in g.log.read_bytes()[start:]
    # Choosing another executable clears both broad and scoped grants.
    g.click(255,148)
    for _ in range(15):g.key('backspace')
    g.type('hello.nos');g.screenshot('new-executable-clears-file-grants')
    start=g.log.stat().st_size;g.click(430,148)
    g.wait(b'DESKTOP PROGRAM RESULT',start)
    assert b'EXIT CODE 42' in g.log.read_bytes()[start:]
    g.click(270,562);start=g.log.stat().st_size;g.key('s','ctrl');g.wait(b'DESKTOP NOTE SAVED',start)
    start=g.log.stat().st_size;g.key('q','ctrl','alt');g.wait(b'DESKTOP EXIT CLEAN',start);g.wait(b'\r\n> ',start)
    assert pages(g.shell('mem',b'HEAP FREE BYTES'))==baseline
    serial=g.log.read_bytes();(folder/'serial.log').write_bytes(serial)
    assert b'SCOPED APP FAIL' not in serial and serial.count(b'SCOPED APP PASS')==3

after=snapshot_files(disk)
expected['sentinel.txt']=b'exit'
assert after=={**expected,'note001.txt':note.encode()}
report.write_text(json.dumps({'passed':True,'checks':[
    'actual GUI editor with read/write/create/delete controls', 'duplicate scopes rejected before Run',
    'read-only grant admits reads and denies writes', 'argument change cannot expand authority',
    'write-only C helpers work on existing target without CREATE', 'scoped events rendered',
    'active launch settings cannot change', 'Stop discards allowed private draft',
    'changing executable clears all grants', 'unsaved note and unrelated files preserved',
    'only permitted target changed, note saved exactly, PMM baseline exact']},indent=2)+'\n')
print('PASS: actual GUI file scopes, deny/allow, frozen settings, Stop and preserved note')
