"""Actual FS #PF during native job IO, while preserving the desktop.

The existing fault_after_io hook is armed through this guest's loopback GDB
stub. No new production debug syscall; no host/working-VM memory is touched.
RSP m/M packets: https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html
"""
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro
from desktop_guest import DesktopGuest
from native_fixture import seed_native_disk
from test_compiler import snapshot_files

folder = ROOT / 'build/native-recovery'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
disk = folder / 'native-recovery-test-only.img'
os.environ.update(NEUROOS_DESKTOP='1', NEUROOS_AI_DEMO='0', NEUROOS_DATA_DISK=str(disk))
target = neuro.build()
expected = seed_native_disk(disk, target)
nm = neuro.tool('LLVM_NM', 'llvm-nm', [r'C:\Program Files\LLVM\bin\llvm-nm.exe'])
symbols = subprocess.check_output([nm, str(target / 'esp/boot/kernel.elf')], text=True)
match = re.search(r'^([0-9a-fA-F]+)\s+\w\s+fault_after_io$', symbols, re.MULTILINE)
assert match, 'Existing IO fault-injection counter missing'
address = int(match[1], 16)
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
original = neuro.qemu_command
neuro.qemu_command = lambda *args, **kwargs: original(*args, **kwargs) + ['-gdb', f'tcp:127.0.0.1:{port}']

def packet(connection, text):
    def exact(count):
        data = bytearray()
        while len(data) < count:
            part = connection.recv(count - len(data))
            if not part: raise RuntimeError('GDB disconnected')
            data += part
        return bytes(data)
    payload = text.encode()
    connection.sendall(b'$' + payload + b'#' + f'{sum(payload) & 255:02x}'.encode())
    while exact(1) != b'$':
        pass
    reply = bytearray()
    while True:
        part = exact(1)
        if part == b'#': break
        if not part: raise RuntimeError('GDB disconnected')
        reply += part
    checksum = exact(2)
    assert len(checksum) == 2 and sum(reply) & 255 == int(checksum, 16)
    connection.sendall(b'+')
    # RSP may encode repeated bytes with '*' followed by count + 29.
    decoded = bytearray(); index = 0
    while index < len(reply):
        if reply[index] == 42:
            decoded.extend(bytes([decoded[-1]]) * (reply[index + 1] - 29)); index += 2
        else:
            decoded.append(reply[index]); index += 1
    return bytes(decoded)

def pages(data):
    return int(re.search(rb'PAGES FREE (\d+)', data)[1])

with DesktopGuest(neuro, target, folder) as guest:
    guest.click(730, 564); guest.click(480, 306); guest.wait(b'SHELL READY')
    baseline = pages(guest.shell('mem', b'HEAP FREE BYTES'))
    start = guest.log.stat().st_size
    guest.type('desktop\n'); guest.wait(b'DESKTOP READY', start); time.sleep(.4)
    guest.click(270, 562); guest.click(565, 151); guest.click(480, 306)
    note = 'Recovery keeps this unsaved note.'; guest.type(note)
    guest.click(598, 562); guest.click(255, 148)
    for _ in range(15): guest.key('backspace')
    guest.type('filedemo.nos'); guest.click(300, 187); guest.type('wait')
    guest.click(585, 148)
    start = guest.log.stat().st_size
    guest.click(430, 148); guest.wait(b'JOB IO WAIT', start)
    guest.qmp('stop')
    with socket.create_connection(('127.0.0.1', port), timeout=3) as connection:
        assert packet(connection, f'M{address:x},4:64000000') == b'OK'
        assert packet(connection, f'm{address:x},4') == b'64000000'
    guest.qmp('cont')
    guest.wait(b'TASK 0 FAULT 14 CONTAINED', start)
    recovered = guest.wait(b'DESKTOP PROGRAM RESULT', start)
    assert b'FILESYSTEM SERVICE RECOVERED' in recovered
    guest.screenshot('contained-filesystem-fault')
    # Fresh job, not replay of the failed write, uses a new session after recovery.
    guest.click(255, 148)
    for _ in range(15): guest.key('backspace')
    guest.type('hello.nos'); guest.click(300, 187)
    for _ in range(4): guest.key('backspace')
    guest.type('recovered')
    start = guest.log.stat().st_size
    guest.click(430, 148)
    assert b'ARGUMENT: recovered' in guest.wait(b'DESKTOP PROGRAM RESULT', start)
    guest.click(270, 562)
    start = guest.log.stat().st_size
    guest.key('s', 'ctrl'); guest.wait(b'DESKTOP NOTE SAVED', start)
    guest.screenshot('note-after-filesystem-recovery')
    guest.key('q', 'ctrl', 'alt'); guest.wait(b'DESKTOP EXIT CLEAN', start)
    guest.wait(b'\r\n> ', start)
    assert pages(guest.shell('mem', b'HEAP FREE BYTES')) == baseline
    (folder / 'serial.log').write_bytes(guest.log.read_bytes())
after = snapshot_files(disk)
for name, data in expected.items(): assert after[name] == data
assert after['note001.txt'] == note.encode()
assert 'app.txt' not in after, 'Failed write was replayed or committed'
report.write_text(json.dumps({'passed': True, 'checks': [
    'real FS #PF after 100 PIO words', 'background job detects peer death',
    'ATA reset and fresh FS mount', 'no failed-write replay',
    'next native job uses fresh session', 'unsaved editor preserved exactly',
    'same physical-page baseline', 'seed files byte-identical']}, indent=2) + '\n')
print('PASS: real mid-PIO FS fault, background-job recovery, fresh session, preserved note and no replay')
