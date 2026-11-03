"""Bounded regression suite, always uses an isolated test disk."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main():
    target=ROOT/'build/release-validation';target.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'completed':[]}
    result=target/'result.json';result.write_text(json.dumps(report,indent=2)+'\n')
    disk=target/'release-test-only.img'
    with disk.open('wb') as stream:stream.truncate(64*1024*1024)
    env=dict(os.environ,NEUROOS_DATA_DISK=str(disk))
    commands=[['tests/test_services.py'],['tests/test_format.py'],['tests/test_stdlib.py'],
              ['tests/test_storage.py'],['tests/test_fs3.py'],['scripts/neuro.py','test','--memory','128']]
    commands += [['tests/test_shell.py',*mode] for mode in
                 ([],['--file-sdk'],['--user-heap'],['--stdio'],['--descriptors'],['--libc'],['--floats'])]
    if '--desktop' in sys.argv:
        commands += [['tests/test_text.py'],['tests/test_desktop.py'],['tests/test_desktop_workflow.py']]
    if '--ai' in sys.argv:
        commands += [['tests/test_ai_guest.py'],['tests/test_ai_desktop.py']]
    for index,command in enumerate(commands):
        name='-'.join(Path(part).stem.lstrip('-') for part in command)
        print('CHECK:',name,flush=True)
        subprocess.run([sys.executable,*command],cwd=ROOT,env=env,check=True)
        if command[0]=='tests/test_shell.py':
            for source in ('serial.log','shell-test.json'):
                shutil.copyfile(ROOT/'build/normal'/source,target/f'{index:02}-{name}-{source}')
        report['completed'].append(command);result.write_text(json.dumps(report,indent=2)+'\n')
    report['passed']=True;result.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: release regression suite; working VM data disk was not used')

if __name__=='__main__':main()
