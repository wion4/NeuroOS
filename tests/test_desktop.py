"""Exercise real desktop pixels, PS/2 input and persistence on an isolated disk."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro

def main():
    os.environ['NEUROOS_DESKTOP']='1'
    folder=ROOT/'build/desktop-integration';folder.mkdir(parents=True,exist_ok=True)
    disk=folder/'desktop-test-only.img'
    with disk.open('wb') as f:f.truncate(64*1024*1024)
    os.environ['NEUROOS_DATA_DISK']=str(disk)
    report=folder/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
    target=neuro.build()
    for cold in (False,True):
        log=target/'serial.log';log.write_bytes(b'')
        with socket.socket() as probe:
            probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
        cmd=neuro.qemu_command(target,False,128)+['-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
        with (folder/('cold-qemu.log' if cold else 'qemu.log')).open('wb') as err:
            proc=subprocess.Popen(cmd,cwd=target,stdout=err,stderr=err)
            try:
                deadline=time.monotonic()+35
                while True:
                    try:conn=socket.create_connection(('127.0.0.1',port),timeout=2);break
                    except OSError:
                        if time.monotonic()>deadline:raise RuntimeError('QMP unavailable')
                        time.sleep(.1)
                with conn:
                    stream=conn.makefile('rwb',buffering=0);json.loads(stream.readline())
                    def qmp(command,args=None):
                        data={'execute':command}
                        if args is not None:data['arguments']=args
                        stream.write(json.dumps(data).encode()+b'\n')
                        while True:
                            answer=json.loads(stream.readline())
                            if 'error' in answer:raise RuntimeError(answer)
                            if 'return' in answer:return answer['return']
                    qmp('qmp_capabilities')
                    def wait(text,start=0,timeout=30):
                        end=time.monotonic()+timeout
                        while time.monotonic()<end:
                            data=log.read_bytes()[start:]
                            assert b'PANIC:' not in data,data[-1200:]
                            full=log.read_bytes()
                            if b'DESKTOP READY' in full:
                                assert b'BUDGET EXPIRED' not in full.split(b'DESKTOP READY',1)[1],full[-1200:]
                            if text in data:return data
                            if proc.poll() is not None:raise RuntimeError('Guest exited')
                            time.sleep(.05)
                        raise AssertionError((text,log.read_bytes()[-1700:]))
                    wait(b'DESKTOP READY');assert b'MOUSE READY' in log.read_bytes()
                    time.sleep(1)
                    mx,my=400,300
                    def move(x,y):
                        nonlocal mx,my
                        while (mx,my)!=(x,y):
                            dx=max(-60,min(60,x-mx));dy=max(-60,min(60,y-my))
                            events=[]
                            if dx:events.append({'type':'rel','data':{'axis':'x','value':dx}})
                            if dy:events.append({'type':'rel','data':{'axis':'y','value':dy}})
                            qmp('input-send-event',{'events':events});mx+=dx;my+=dy;time.sleep(.045)
                    def click(x,y):
                        move(x,y)
                        for down in (True,False):
                            qmp('input-send-event',{'events':[{'type':'btn','data':{'down':down,'button':'left'}}]})
                            time.sleep(.12)
                        time.sleep(.15)
                    def key(k,shift=False):
                        keys=['shift',k] if shift else [k]
                        for code in keys:
                            qmp('input-send-event',{'events':[{'type':'key','data':{'down':True,'key':{'type':'qcode','data':code}}}]});time.sleep(.035)
                        for code in reversed(keys):
                            qmp('input-send-event',{'events':[{'type':'key','data':{'down':False,'key':{'type':'qcode','data':code}}}]});time.sleep(.035)
                    def type_text(s):
                        mapping={' ':'spc','\n':'ret','.':'dot','-':'minus','+':'equal','/':'slash'}
                        for char in s:key(mapping.get(char,char.lower()),char.isupper() or char=='+')
                    def screenshot(name):
                        path=folder/(name+'.ppm');qmp('screendump',{'filename':str(path)})
                        from PIL import Image
                        with Image.open(path) as im:im.save(folder/(name+'.png'))
                    if not cold:
                        screenshot('welcome')
                        # Notes dock; replace the initial greeting by starting a new note.
                        click(270,562)
                        click(565,151) # New
                        click(480,306) # Confirm discard initial unsaved greeting
                        type_text('Saved across a cold boot.\nSecond line.')
                        start=log.stat().st_size;click(490,151);wait(b'DESKTOP NOTE SAVED',start)
                        # Actual Ctrl+S through the keyboard driver, with an edited buffer.
                        type_text('x');key('backspace')
                        start=log.stat().st_size
                        qmp('input-send-event',{'events':[{'type':'key','data':{'down':True,'key':{'type':'qcode','data':'ctrl'}}}]})
                        key('s')
                        qmp('input-send-event',{'events':[{'type':'key','data':{'down':False,'key':{'type':'qcode','data':'ctrl'}}}]})
                        wait(b'DESKTOP NOTE SAVED',start)
                        screenshot('notes')
                        # Calculator through both keyboard and pointer controls.
                        click(380,562);type_text('12+30');key('ret');wait(b'DESKTOP CALC RESULT')
                        screenshot('calculator')
                        # Appearance and persistent preferences.
                        click(490,562);start=log.stat().st_size;click(465,275);wait(b'DESKTOP THEME SAVED',start)
                        screenshot('appearance')
                        click(160,562);screenshot('files')
                        # Drag title bar, preserving desktop operation afterwards.
                        move(260,91)
                        qmp('input-send-event',{'events':[{'type':'btn','data':{'down':True,'button':'left'}}]})
                        time.sleep(.12);move(280,107)
                        qmp('input-send-event',{'events':[{'type':'btn','data':{'down':False,'button':'left'}}]});time.sleep(.2)
                        screenshot('dragged')
                        # Stay alive past the old 10 CPU-second application budget.
                        until=time.monotonic()+12
                        while time.monotonic()<until:
                            move(401 if mx!=401 else 400,300);time.sleep(.2)
                        assert b'BUDGET EXPIRED' not in log.read_bytes().split(b'DESKTOP READY',1)[1]
                    else:
                        # Open the actual file via the browser after a fresh kernel start.
                        click(160,562);click(195,204);click(395,142)
                        click(480,306) # Discard fresh greeting
                        wait(b'DESKTOP NOTE OPENED')
                        screenshot('cold-note')
                    # Console exit, then inspect disk bytes through a different client.
                    click(730,564)
                    if cold:click(480,306) # only if the unsaved greeting still needed confirmation
                    wait(b'SHELL READY');type_text('cat note001.txt\n')
                    wait(b'Saved across a cold boot.\r\nSecond line.')
                    type_text('cat desktop.cfg\n');wait(b'2\r\n> ')
                    if not cold:
                        type_text('write protected keep me\n');wait(b'WROTE 7 BYTES')
                        start=log.stat().st_size;type_text('desktop\n');wait(b'DESKTOP READY',start)
                        time.sleep(.5)
                        click(270,562);click(285,151)
                        for _ in range(8):key('backspace') # default note.txt
                        type_text('protected')
                        click(490,151);screenshot('overwrite-confirmation')
                        click(330,306) # Cancel replacement
                        click(730,564);click(480,306) # Discard unsaved greeting
                        wait(b'DESKTOP CLOSED',start)
                        type_text('cat protected\n');wait(b'keep me\r\n')
                    else:
                        # A pre-existing non-setting user file must never be replaced.
                        type_text('write desktop.cfg user data\n');wait(b'WROTE 9 BYTES')
                        start=log.stat().st_size;type_text('desktop\n');wait(b'DESKTOP READY',start)
                        time.sleep(.5);click(490,562);click(465,275)
                        screenshot('settings-conflict')
                        assert b'DESKTOP THEME SAVED' not in log.read_bytes()[start:]
                        click(730,564);click(480,306);wait(b'DESKTOP CLOSED',start)
                        type_text('cat desktop.cfg\n');wait(b'user data\r\n')
                    (folder/('cold-serial.log' if cold else 'serial.log')).write_bytes(log.read_bytes())
            finally:
                proc.kill();proc.wait()
    report.write_text(json.dumps({'passed':True,'checks':['real PS/2 pointer and keyboard',
        'notes transactional save and cold read','files browser','calculator input',
        'persistent appearance','window drag','idle and relaunch','Ctrl+S',
        'cancel overwrite preserves existing file','settings filename conflict preserves user data',
        '128 MiB boot']},indent=2)+'\n')
    print('PASS: interactive desktop, real input, notes and appearance survive cold boot')

if __name__=='__main__':main()
