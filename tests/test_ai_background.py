"""Real concurrent GUI/FS/inference, cancellation and task reaping at 128 MiB.

Instruction-count pacing makes the overlap observable on fast hosts. This is
still the actual transformer, scheduler and disk service; no inference stubs.
"""
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
from test_compiler import snapshot_files

folder = ROOT / 'build/ai-background'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'ai-background-test-only.img'
with disk.open('wb') as stream:
    stream.truncate(64 * 1024 * 1024)
os.environ.update(NEUROOS_AI_DEMO='1', NEUROOS_DESKTOP='1', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
# Format the initially empty test volume at normal instruction speed. Artificial
# pacing must not turn legacy import's fixed CPU budget into a mount failure.
with DesktopGuest(neuro, target, folder):
    pass
original_command = neuro.qemu_command

def paced_command(*args, **kwargs):
    return original_command(*args, **kwargs) + ['-icount', 'shift=6,align=off,sleep=on']

neuro.qemu_command = paced_command

def pages(data):
    return int(re.search(rb'PAGES FREE (\d+)', data)[1])

with DesktopGuest(neuro, target, folder, boot_timeout=120) as guest:
    guest.click(730, 564)
    guest.click(480, 306)
    guest.wait(b'SHELL READY')
    baseline = pages(guest.shell('mem', b'HEAP FREE BYTES'))
    start = guest.log.stat().st_size
    guest.type('desktop\n')
    guest.wait(b'DESKTOP READY', start)
    time.sleep(.5)
    guest.click(270, 562)
    guest.click(565, 151)
    guest.click(480, 306)
    note = 'Saved during inference.'
    guest.type(note)
    start = guest.log.stat().st_size
    guest.key('g', 'ctrl', 'alt')
    guest.wait(b'AI GUI STARTED', start)
    guest.key('s', 'ctrl')
    overlap = guest.wait(b'DESKTOP NOTE SAVED', start, 60)
    if b'AI GUI COMPLETE' in overlap:
        assert overlap.index(b'DESKTOP NOTE SAVED') < overlap.index(b'AI GUI COMPLETE'), \
            'Test did not overlap inference and save'
    guest.screenshot('notes-during-inference')
    guest.wait(b'AI GUI COMPLETE', start, 90)
    guest.click(450, 16)
    guest.screenshot('background-result')
    # Explicit cancellation must reclaim both tasks, then allow a fresh request.
    start = guest.log.stat().st_size
    guest.key('ret')
    guest.wait(b'AI GUI STARTED', start)
    guest.key('esc')
    cancelled = guest.wait(b'AI GUI CANCELLED', start)
    assert b'AI GUI COMPLETE' not in cancelled
    start = guest.log.stat().st_size
    guest.click(202, 218)
    guest.wait(b'AI GUI COMPLETE', start, 90)
    # Keep the runtime cached while switching back to the console path (0/1 IPC).
    guest.click(730, 564)
    guest.wait(b'SHELL READY')
    guest.wait(b'DESKTOP EXIT CLEAN', start)
    guest.shell('ai The cat', b'AI DEMO COMPLETE', 90)
    guest.shell('ai-reset', b'AI RUNTIME RESET')
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    # Closing the entire desktop during inference must reap both live tasks.
    start = guest.log.stat().st_size
    guest.type('desktop\n')
    guest.wait(b'DESKTOP READY', start)
    time.sleep(.5)
    guest.click(270, 562)
    guest.key('s', 'ctrl')
    guest.wait(b'DESKTOP NOTE SAVED', start)
    start = guest.log.stat().st_size
    guest.key('g', 'ctrl', 'alt')
    guest.wait(b'AI GUI STARTED', start)
    guest.key('q', 'ctrl', 'alt')
    closed = guest.wait(b'DESKTOP CLOSED', start)
    assert b'AI GUI COMPLETE' not in closed
    guest.wait(b'\r\n> ', start)
    guest.wait(b'DESKTOP EXIT CLEAN', start)
    guest.shell('ai-reset', b'AI RUNTIME RESET')
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    serial = guest.log.read_bytes()
    assert b'DESKTOP SESSION FAILED' not in serial
    (folder / 'serial.log').write_bytes(serial)

assert snapshot_files(disk)['note001.txt'] == note.encode()
report.write_text(json.dumps({'passed': True, 'checks': [
    'actual CPU inference overlaps GUI Notes save through isolated FS IPC',
    '128 MiB and no guest network', 'cancel reaps both tasks',
    'fresh generation after cancellation', 'cached background to foreground IPC rebinding',
    'desktop exit reaps active inference', 'exact physical-page accounting',
    'saved note independently verified on disk']}, indent=2) + '\n')
print('PASS: background inference, live Notes/FS, cancel, exit, cache rebinding and no page leaks')
