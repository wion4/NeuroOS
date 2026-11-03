"""One configured provisioning boot and a non-destructive repeated boot."""
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
target=ROOT/'build/sdk-bootstrap-test';target.mkdir(parents=True,exist_ok=True)
disk=target/'bootstrap-test-only.img'
with disk.open('wb') as stream:stream.truncate(64*1024*1024)
env=dict(os.environ,NEUROOS_FS_BACKEND='3',NEUROOS_BOOTSTRAP_SDK='1',NEUROOS_DATA_DISK=str(disk))
for mode in ('--sdk-auto-first','--sdk-auto-read'):
    subprocess.run([sys.executable,str(ROOT/'tests/test_shell.py'),mode],cwd=ROOT,env=env,check=True)
print('PASS: automatic SDK provisioning and idempotent cold boot; real VM disk was not used')
