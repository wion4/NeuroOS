"""Real user-space inference in a network-free 128/256 MiB guest, own data disk."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'build/ai-integration';folder.mkdir(parents=True,exist_ok=True)
result=folder/'result.json';result.write_text('{"passed":false,"status":"running"}\n')
disk=folder/'ai-test-only.img'
with disk.open('wb') as stream:stream.truncate(64*1024*1024)
subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tests/test_ai_core.py')],cwd=ROOT,check=True)
env=dict(os.environ,NEUROOS_AI_DEMO='1',NEUROOS_DATA_DISK=str(disk))
env.pop('NEUROOS_FS3_TEST',None);env.pop('NEUROOS_DESKTOP',None)
subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tests/test_shell.py'),'--ai'],cwd=ROOT,env=env,check=True)
(folder/'serial.log').write_bytes((ROOT/'build/normal-ai/serial.log').read_bytes())
result.write_text(json.dumps({'passed':True,'checks':['real pinned transformer weights',
    'no network in guest','exact greedy token parity with host oracle','persistent warm runtime',
    'read-only model cap and no disk ports or filesystem grants','contained runtime fault',
    'next-request restart without replay','reset releases physical pages','sentinel unchanged']},indent=2)+'\n')
print('PASS: isolated offline transformer service in NeuroOS')
