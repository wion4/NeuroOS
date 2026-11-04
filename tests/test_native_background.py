"""Actual GUI editing/save while a normal .nos runs; no model and own disk."""
import json
import os
from pathlib import Path
import re
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from test_compiler import snapshot_files

folder = ROOT / 'build/native-background'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'native-background-test-only.img'
os.environ.update(NEUROOS_DESKTOP='1', NEUROOS_AI_DEMO='0', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
expected = seed_native_disk(disk, target)

def pages(data):
    return int(re.search(rb'PAGES FREE (\d+)', data)[1])

with DesktopGuest(neuro, target, folder) as guest:
    guest.click(730, 564); guest.click(480, 306)
    guest.wait(b'SHELL READY')
    baseline = pages(guest.shell('mem', b'HEAP FREE BYTES'))
    start = guest.log.stat().st_size
    guest.type('desktop\n'); guest.wait(b'DESKTOP READY', start); time.sleep(.4)
    guest.click(270, 562); guest.click(565, 151); guest.click(480, 306)
    guest.type('Parallel ')
    guest.click(598, 562)
    previous_name, previous_args = 'hello.nos', ''

    def fields(name, args=''):
        global previous_name, previous_args
        if name != previous_name:
            guest.click(255, 148)
            for _ in range(15): guest.key('backspace')
            guest.type(name)
        guest.click(300, 187)
        for _ in range(len(previous_args)): guest.key('backspace')
        guest.type(args)
        previous_name, previous_args = name, args

    fields('spin.nos')
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'DESKTOP PROGRAM STARTED', start)
    guest.click(270, 562); guest.type('editing')
    guest.key('s', 'ctrl')
    overlap = guest.wait(b'DESKTOP NOTE SAVED', start)
    assert b'DESKTOP PROGRAM RESULT' not in overlap and b'CPU BUDGET EXPIRED' not in overlap
    guest.screenshot('editing-during-native-program')
    guest.click(598, 562); guest.click(430, 148)
    guest.wait(b'DESKTOP PROGRAM RESULT', start)
    assert b'NATIVE JOB CANCELLED' in guest.log.read_bytes()[start:]
    guest.click(270, 562); guest.type(' preserved.'); guest.key('s', 'ctrl')
    guest.click(598, 562)
    fields('filedemo.nos', 'job'); guest.click(585, 148)
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'JOB PRIVATE TRANSACTION OPEN', start)
    guest.click(585, 148); guest.wait(b'DESKTOP JOB SETTINGS LOCKED', start)
    guest.click(270, 562); guest.type(' next'); guest.key('s', 'ctrl')
    guest.wait(b'DESKTOP NOTE SAVE BUSY', start)
    assert b'DESKTOP PROGRAM RESULT' not in guest.log.read_bytes()[start:]
    guest.screenshot('private-transaction-busy')
    guest.click(598, 562); guest.click(430, 148)
    completed = guest.wait(b'DESKTOP PROGRAM RESULT', start)
    assert b'NATIVE JOB CANCELLED' in completed and b'NATIVE JOB FS SESSION CLOSED' in completed
    guest.click(270, 562)
    start = guest.log.stat().st_size
    guest.key('s', 'ctrl'); guest.wait(b'DESKTOP NOTE SAVED', start)
    # A clean parent exit cancels its sleeping child and releases the draft.
    guest.click(598, 562)
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'JOB PRIVATE TRANSACTION OPEN', start)
    guest.key('q', 'ctrl', 'alt')
    guest.wait(b'DESKTOP EXIT CLEAN', start)
    guest.wait(b'\r\n> ', start)
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    serial = guest.log.read_bytes()
    assert b'DESKTOP SESSION FAILED' not in serial
    (folder / 'serial.log').write_bytes(serial)

after = snapshot_files(disk)
for name, data in expected.items(): assert after[name] == data, name
assert after['note001.txt'] == b'Parallel editing preserved. next'
assert 'job-draft.tmp' not in after
report.write_text(json.dumps({'passed': True, 'checks': [
    '128 MiB guest without model/network', 'edit and save before CPU-bound child stops',
    'Stop cancels CPU-bound and sleeping children', 'foreign draft is private and save fails busy',
    'retry saves the unchanged editor after cancellation', 'session close rolls back draft',
    'parent exit terminates child', 'exact PMM baseline after exit',
    'saved note and all seed files independently verified']}, indent=2) + '\n')
print('PASS: live native GUI editing/save, Stop, transaction isolation, rollback and parent-exit cleanup')
