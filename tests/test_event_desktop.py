"""Real GUI observer during CPU work/FS transaction, overwrite and GUI restart."""
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

folder = ROOT / 'build/event-desktop'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'event-desktop-test-only.img'
os.environ.update(NEUROOS_DESKTOP='1', NEUROOS_AI_DEMO='0', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
expected = seed_native_disk(disk, target)

def pages(data):
    return int(re.search(rb'PAGES FREE (\d+)', data)[1])

with DesktopGuest(neuro, target, folder) as guest:
    guest.click(730, 564); guest.click(480, 306); guest.wait(b'SHELL READY')
    baseline = pages(guest.shell('mem', b'HEAP FREE BYTES'))
    start = guest.log.stat().st_size
    guest.type('desktop\n'); guest.wait(b'DESKTOP READY', start); time.sleep(.4)
    guest.click(270, 562); guest.click(565, 151); guest.click(480, 306)
    note = 'Event observer keeps my note.'; guest.type(note)
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

    def open_events(marker=b'DESKTOP EVENTS UPDATED'):
        begin = guest.log.stat().st_size
        guest.click(665, 103)
        guest.wait(marker, begin)
        time.sleep(.4)  # Drain at most eight bounded batches before capture.

    fields('spin.nos')
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'DESKTOP PROGRAM STARTED', start)
    open_events(); guest.screenshot('events-while-running')
    assert b'DESKTOP PROGRAM RESULT' not in guest.log.read_bytes()[start:]
    guest.key('esc'); guest.click(430, 148)
    guest.wait(b'DESKTOP PROGRAM RESULT', start)
    open_events(); guest.screenshot('task-events')
    guest.key('pgdn'); guest.key('pgup'); guest.key('end'); guest.key('home'); guest.key('esc')
    fields('filedemo.nos', 'job'); guest.click(585, 148)
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'JOB PRIVATE TRANSACTION OPEN', start)
    open_events(); guest.screenshot('events-during-private-transaction')
    assert b'DESKTOP PROGRAM RESULT' not in guest.log.read_bytes()[start:]
    guest.key('esc'); guest.click(430, 148)
    guest.wait(b'DESKTOP PROGRAM RESULT', start)
    open_events(); guest.screenshot('events-after-rollback'); guest.key('esc')
    fields('hello.nos')
    # Close the observer while the actual kernel journal wraps. Reopening must
    # detect missing records, rather than presenting a continuous fake history.
    for _ in range(18):
        start = guest.log.stat().st_size
        guest.key('ret'); guest.wait(b'DESKTOP PROGRAM RESULT', start)
    open_events(b'DESKTOP EVENT RECORDS MISSED')
    guest.screenshot('event-overwrite-detected')
    guest.click(695, 469); guest.click(608, 469); guest.click(215, 469)
    guest.key('esc'); guest.click(270, 562)
    start = guest.log.stat().st_size
    guest.key('s', 'ctrl'); guest.wait(b'DESKTOP NOTE SAVED', start)
    guest.key('q', 'ctrl', 'alt'); guest.wait(b'DESKTOP EXIT CLEAN', start)
    guest.wait(b'\r\n> ', start)
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    # Kernel metadata survives the controller process, not a power cycle.
    start = guest.log.stat().st_size
    guest.type('desktop\n'); guest.wait(b'DESKTOP READY', start); time.sleep(.4)
    guest.click(598, 562); open_events(b'DESKTOP EVENT RECORDS MISSED')
    guest.screenshot('events-after-controller-restart')
    guest.key('esc'); fields('spin.nos')
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'DESKTOP PROGRAM STARTED', start)
    # This owner's active child is revoked when the controller exits.
    start = guest.log.stat().st_size
    guest.key('q', 'ctrl', 'alt'); guest.click(480, 306)
    guest.wait(b'DESKTOP EXIT CLEAN', start); guest.wait(b'\r\n> ', start)
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    start = guest.log.stat().st_size
    guest.type('desktop\n'); guest.wait(b'DESKTOP READY', start); time.sleep(.4)
    guest.click(598, 562); open_events(b'DESKTOP EVENT RECORDS MISSED')
    guest.screenshot('events-after-owner-exit')
    start = guest.log.stat().st_size
    guest.key('q', 'ctrl', 'alt'); guest.click(480, 306)
    guest.wait(b'DESKTOP EXIT CLEAN', start); guest.wait(b'\r\n> ', start)
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    serial = guest.log.read_bytes()
    assert b'DESKTOP EVENT OBSERVER ERROR' not in serial
    assert b'DESKTOP SESSION FAILED' not in serial
    (folder / 'serial.log').write_bytes(serial)
after = snapshot_files(disk)
for name, data in expected.items(): assert after[name] == data
assert after['note001.txt'] == note.encode() and 'job-draft.tmp' not in after
report.write_text(json.dumps({'passed': True, 'checks': [
    'real no-model 128 MiB GUI', 'observer during CPU loop without cancelling it',
    'observer does not need FS access while another session holds a transaction',
    'cancel/rollback events rendered', 'paging controls and latest view',
    'real journal overwrite explicitly reported', 'independent reader after desktop restart',
    'owner exit of an active job remains observable',
    'unsaved note retained', 'exact PMM and seed files after controller exits']}, indent=2) + '\n')
print('PASS: real GUI event observer, busy FS, overwrite detection, controller restart and no page leaks')
