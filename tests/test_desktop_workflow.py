"""RU input, UTF-8 editing and isolated native launch through the actual desktop."""
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
from desktop_guest import DesktopGuest
from test_compiler import snapshot_files

def main():
    folder=ROOT/'build/desktop-workflow';folder.mkdir(parents=True,exist_ok=True)
    report=folder/'result.json';report.write_text('{"passed":false,"status":"running"}\n')
    os.environ['NEUROOS_DESKTOP']='1';disk=folder/'desktop-workflow-test-only.img'
    os.environ['NEUROOS_DATA_DISK']=str(disk)
    target=neuro.build()
    hello=(target/'esp/boot/hello.nos').read_bytes();spin=bytearray(hello);spin[128:130]=b'\xeb\xfe'
    payload=bytearray(131264)
    seed=[('hello.nos',hello),('filedemo.nos',(target/'esp/boot/filedemo.nos').read_bytes()),('spin.nos',spin),('bad.nos',b'invalid'),('utf8.txt','Файл с диска: Ёжик.\n'.encode()),('source.c',b'int main(void){return 73;}')]
    seed.append(('other.nos',(target/'esp/boot/filedemo.nos').read_bytes()))
    for i,(name,data) in enumerate(seed):
        assert len(data)<=16384
        offset=i*16408;payload[offset:offset+len(name)]=name.encode()
        struct.pack_into('<II',payload,offset+16,1,len(data));payload[offset+24:offset+24+len(data)]=data
    header=bytearray(512);struct.pack_into('<4Q',header,0,0x3253464f5255454e,3,len(payload),zlib.crc32(payload));struct.pack_into('<Q',header,32,zlib.crc32(header[:32]))
    with disk.open('wb') as f:f.truncate(64*1024*1024);f.seek(64*512);f.write(header+payload)
    expected='Привет, мир! Ёжик.\n'
    with DesktopGuest(neuro,target,folder) as g:
        # Leave the desktop once for an independent baseline counter.
        g.click(730,564);g.click(480,306);g.wait(b'SHELL READY')
        g.shell('install compiler.nos',b'INSTALLED compiler.nos')
        baseline=g.shell('mem',b'HEAP FREE BYTES')
        start=g.log.stat().st_size;g.type('desktop\n');g.wait(b'DESKTOP READY',start);time.sleep(.4)
        g.click(270,562);g.click(565,151);g.click(480,306)
        # Russian layout from the top bar; physical JCUKEN input, not pasted Unicode.
        g.click(550,16)
        g.key('g','shift');g.type('hbdtn');g.key('slash','shift');g.type(' vbh');g.key('1','shift');g.type(' ')
        g.key('grave_accent','shift');g.type(';br');g.key('slash');g.key('ret')
        g.key('shift','alt');g.key('shift','alt') # Same RU layout after two physical toggles
        # Ctrl+A/C/V, undo and redo use physical Latin shortcuts even in RU.
        g.key('a','ctrl');g.key('c','ctrl');g.key('end','ctrl');g.key('v','ctrl')
        g.key('z','ctrl');g.key('y','ctrl');g.key('z','ctrl') # Back to one copy
        g.screenshot('russian-note')
        g.click(685,107);g.screenshot('maximized-note');g.click(735,52)
        # Keep the note unsaved while unrelated processes execute and fault.
        g.click(550,16) # EN for program filenames/args
        g.click(598,562)
        previous_arguments=''
        previous_program='hello.nos'
        def field(name,args=''):
            nonlocal previous_arguments,previous_program
            if name!=previous_program:
                g.click(255,148)
                for _ in range(15):g.key('backspace')
                g.type(name)
            previous_program=name;g.click(300,187)
            for _ in range(len(previous_arguments)):g.key('backspace')
            g.type(args)
            previous_arguments=args
        def run(expected_marker):
            start=g.log.stat().st_size;g.click(430,148)
            data=g.wait(b'DESKTOP PROGRAM RESULT',start,40)
            assert expected_marker in data,data
            return data
        field('hello.nos','desktop argument');run(b'ARGUMENT: desktop argument')
        g.screenshot('native-hello')
        field('filedemo.nos');run(b'FILE ACCESS DENIED OR FAILED')
        g.click(585,148);run(b'C FILE LIBRARY PASS') # Explicit FS grant
        field('filedemo.nos','backlog');run(b'NATIVE JOB FS SESSION CLOSED')
        field('filedemo.nos','crash');run(b'FAULT 14 CONTAINED')
        g.screenshot('contained-fault')
        field('other.nos');run(b'FILE ACCESS DENIED OR FAILED')
        field('bad.nos');run(b'DESKTOP CHILD FINISHED')
        field('spin.nos');run(b'CPU BUDGET EXPIRED')
        g.screenshot('bounded-program')
        g.click(160,562);g.click(200,339);g.click(525,142) # source.c -> Build C
        previous_arguments='source.c source.nos'
        previous_program='compiler.nos'
        g.click(585,148);run(b'COMPILED source.c -> source.nos')
        g.screenshot('native-compile')
        field('source.nos');run(b'EXIT CODE 73')
        # All of those kept the original editor address space, history and selection.
        g.click(270,562);start=g.log.stat().st_size;g.key('s','ctrl');g.wait(b'DESKTOP NOTE SAVED',start)
        g.click(730,564);g.wait(b'SHELL READY')
        after=g.shell('mem',b'HEAP FREE BYTES')
        assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',after)[1],(baseline,after)
        g.shell('cat app.txt',b'written by C')
        (folder/'serial.log').write_bytes(g.log.read_bytes())
    files=snapshot_files(disk);assert files['note001.txt']==expected.encode(),files['note001.txt']
    assert files['app.txt']==b'written by C'
    note_index=list(files).index('note001.txt')
    with DesktopGuest(neuro,target,folder) as g:
        g.click(598,562);g.click(255,148)
        for _ in range(15):g.key('backspace')
        g.type('source.nos');start=g.log.stat().st_size;g.click(430,148)
        data=g.wait(b'DESKTOP PROGRAM RESULT',start);assert b'EXIT CODE 73' in data
        g.click(160,562)
        for _ in range(note_index//9):g.click(675,458)
        g.click(200,204+(note_index%9)*27);g.click(395,142);g.click(480,306)
        g.wait(b'DESKTOP NOTE OPENED');g.screenshot('cold-russian-note')
        g.click(730,564);g.wait(b'SHELL READY')
        (folder/'cold-serial.log').write_bytes(g.log.read_bytes())
    files=snapshot_files(disk);assert files['note001.txt']==expected.encode()
    report.write_text(json.dumps({'passed':True,'checks':['physical RU input and Cyrillic font',
        'UTF-8 byte-exact cold persistence','clipboard/selection/undo/redo',
        'native args and captured output','default denied FS grant then explicit allow',
        'user fault containment','invalid NOS rejection','CPU limit and desktop resume',
        'desktop survives stale IPC and fresh filesystem server',
        'changing the executable clears previous file grants',
        'GUI native C compilation and cold executable launch','maximize and restore Notes',
        'unsaved editor survives child launches','physical page accounting']},indent=2)+'\n')
    print('PASS: UTF-8 desktop editing, native launches, grants, faults, CPU limit and cold boot')
if __name__=='__main__':main()
