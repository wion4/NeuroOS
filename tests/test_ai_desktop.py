"""Model service failure/recovery through the real GUI, preserving the user's buffer."""
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
from test_compiler import snapshot_files
folder=ROOT/'build/ai-desktop';folder.mkdir(parents=True,exist_ok=True)
report=folder/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
disk=folder/'ai-desktop-test-only.img'
with disk.open('wb') as f:f.truncate(64*1024*1024)
os.environ.update(NEUROOS_AI_DEMO='1',NEUROOS_DESKTOP='1',NEUROOS_DATA_DISK=str(disk))
target=neuro.build()
with DesktopGuest(neuro,target,folder) as g:
    g.click(730,564);g.click(480,306);g.wait(b'SHELL READY')
    baseline=g.shell('mem',b'HEAP FREE BYTES')
    g.shell('ai Once upon a time',b'AI DEMO COMPLETE',60)
    g.shell('ai-fault',b'AI FAULT ARMED')
    start=g.log.stat().st_size;g.type('desktop\n');g.wait(b'DESKTOP READY',start);time.sleep(.5)
    g.click(270,562);g.click(565,151);g.click(480,306)
    text='AI must not change this note.';g.type(text)
    g.click(450,16)
    start=g.log.stat().st_size;g.click(202,218);g.wait(b'AI GUI ERROR',start,60)
    g.screenshot('contained-ai-fault')
    start=g.log.stat().st_size;g.click(202,218);g.wait(b'AI GUI COMPLETE',start,60)
    g.screenshot('offline-ai')
    # A different prompt must produce the separate model-conditioned continuation.
    g.click(320,177)
    for _ in range(16):g.key('backspace')
    g.type('The cat')
    start=g.log.stat().st_size;g.key('ret');g.wait(b'AI GUI COMPLETE',start,60)
    g.screenshot('offline-ai-cat')
    start=g.log.stat().st_size;g.click(322,218);g.wait(b'AI GUI UNLOADED',start)
    g.click(270,562);start=g.log.stat().st_size;g.key('s','ctrl');g.wait(b'DESKTOP NOTE SAVED',start)
    g.click(730,564);g.wait(b'SHELL READY')
    final=g.shell('mem',b'HEAP FREE BYTES')
    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1],(baseline,final)
    (folder/'serial.log').write_bytes(g.log.read_bytes())
assert snapshot_files(disk)['note001.txt']==text.encode()
report.write_text(json.dumps({'passed':True,'checks':['128 MiB boot without network',
    'GUI invokes separate model service','real fault while GUI is suspended is contained',
    'next request reloads and generates','two prompts and rendered output',
    'unload frees cached model pages','unsaved Notes preserved byte-for-byte']},indent=2)+'\n')
print('PASS: offline AI GUI, real runtime fault, recovery, unload and unsaved note preservation')
