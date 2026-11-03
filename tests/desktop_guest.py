"""Headless real-guest input and framebuffer helper; never uses the Windows mouse."""
import json
from pathlib import Path
import socket
import subprocess
import time
class DesktopGuest:
    def __init__(self,neuro,target,folder,boot_timeout=35):
        self.target=target;self.folder=folder;self.log=target/'serial.log';self.log.write_bytes(b'')
        with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
        cmd=neuro.qemu_command(target,False,128)+['-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
        self.error=(folder/'qemu.log').open('ab');self.proc=subprocess.Popen(cmd,cwd=target,stdout=self.error,stderr=self.error)
        try:
            until=time.monotonic()+boot_timeout
            while True:
                try:self.conn=socket.create_connection(('127.0.0.1',port),timeout=3);break
                except OSError:
                    if time.monotonic()>until:raise RuntimeError('QMP unavailable')
                    time.sleep(.1)
            self.stream=self.conn.makefile('rwb',buffering=0);json.loads(self.stream.readline())
            self.qmp('qmp_capabilities');self.wait(b'DESKTOP READY',timeout=boot_timeout);time.sleep(.5)
            self.x,self.y=400,300
        except BaseException:self.close();raise
    def qmp(self,name,args=None):
        q={'execute':name}
        if args is not None:q['arguments']=args
        self.stream.write(json.dumps(q).encode()+b'\n')
        while True:
            answer=json.loads(self.stream.readline())
            if 'error' in answer:raise RuntimeError(answer)
            if 'return' in answer:return answer['return']
    def wait(self,text,start=0,timeout=30):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            data=self.log.read_bytes()[start:]
            assert b'PANIC:' not in data,data[-1800:]
            if text in data:return data
            if self.proc.poll() is not None:raise RuntimeError('Guest exited')
            time.sleep(.05)
        raise AssertionError((text,self.log.read_bytes()[-2000:]))
    def move(self,x,y):
        while (self.x,self.y)!=(x,y):
            dx=max(-60,min(60,x-self.x));dy=max(-60,min(60,y-self.y));events=[]
            if dx:events.append({'type':'rel','data':{'axis':'x','value':dx}})
            if dy:events.append({'type':'rel','data':{'axis':'y','value':dy}})
            self.qmp('input-send-event',{'events':events});self.x+=dx;self.y+=dy;time.sleep(.045)
    def button(self,down):
        self.qmp('input-send-event',{'events':[{'type':'btn','data':{'down':down,'button':'left'}}]});time.sleep(.13)
    def click(self,x,y):self.move(x,y);self.button(True);self.button(False);time.sleep(.15)
    def event(self,key,down):
        self.qmp('input-send-event',{'events':[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':key}}}]});time.sleep(.035)
    def key(self,key,*modifiers):
        for k in (*modifiers,key):self.event(k,True)
        for k in reversed((*modifiers,key)):self.event(k,False)
    def type(self,text):
        plain={' ':'spc','\n':'ret','.':'dot','-':'minus','/':'slash','_':'minus',';':'semicolon'}
        shift={'+':'equal','!':'1','?':'slash',':':'semicolon'}
        for ch in text:
            self.key(shift.get(ch,plain.get(ch,ch.lower())),*(['shift'] if ch.isupper() or ch in shift or ch=='_' else []))
    def screenshot(self,name):
        from PIL import Image
        path=self.folder/(name+'.ppm');self.qmp('screendump',{'filename':str(path)})
        with Image.open(path) as img:img.save(self.folder/(name+'.png'))
    def shell(self,command,expected,timeout=30):
        start=self.log.stat().st_size;self.type(command+'\n');self.wait(expected,start,timeout);self.wait(b'\r\n> ',start,timeout)
        return self.log.read_bytes()[start:]
    def close(self):
        if getattr(self,'proc',None) is not None:self.proc.kill();self.proc.wait()
        if getattr(self,'stream',None) is not None:self.stream.close()
        if getattr(self,'conn',None) is not None:self.conn.close()
        if getattr(self,'error',None) is not None:self.error.close()
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
