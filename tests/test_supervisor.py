"""Bounded service crash/restart scenarios against an isolated data disk."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
target=ROOT/'build/supervisor-integration';target.mkdir(parents=True,exist_ok=True)
report=target/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
disk=target/'supervisor-test-only.img'
with disk.open('wb') as stream:stream.truncate(64*1024*1024)
env=dict(os.environ,NEUROOS_FS_BACKEND='3',NEUROOS_DATA_DISK=str(disk))
for mode in ('--supervisor','--supervisor-read'):
    subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),mode],cwd=ROOT,env=env,check=True)
report.write_text(json.dumps({'passed':True,'checks':['restart on next request, no replay',
    'real fault after 100 ATA data-word transfers','ATA reset clears unfinished transfer',
    'pending transaction rollback','malformed IPC and quit denied',
    'stale session context discarded','three attempts then explicit restart',
    'cold boot data preservation','no leaked physical pages']},indent=2)+'\n')
print('PASS: filesystem supervisor, crash isolation, bounded recovery and cold persistence')
