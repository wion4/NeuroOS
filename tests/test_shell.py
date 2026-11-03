"""Boot the production kernel and inject real keyboard events through QMP."""
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro


def main():
    target = neuro.build()
    (target/'shell-test.json').write_text('{"passed": false, "status": "running"}\n')
    log = target / 'serial.log'
    log.write_bytes(b'')
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
    cmd = neuro.qemu_command(target, False, 256)
    cmd += ['-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off']
    with (target / 'shell-qemu.log').open('wb') as err:
        proc = subprocess.Popen(cmd, cwd=target, stdout=err, stderr=err)
        try:
            deadline = time.monotonic() + 30
            while True:
                try: conn = socket.create_connection(('127.0.0.1', port), timeout=2); break
                except OSError:
                    if time.monotonic() > deadline: raise RuntimeError('QMP unavailable')
                    time.sleep(.1)
            with conn:
                stream = conn.makefile('rwb', buffering=0)
                json.loads(stream.readline())
                def qmp(command, arguments=None):
                    message = {'execute': command}
                    if arguments is not None: message['arguments'] = arguments
                    stream.write(json.dumps(message).encode()+b'\n')
                    while True:
                        answer = json.loads(stream.readline())
                        if 'error' in answer: raise RuntimeError(answer)
                        if 'return' in answer: return answer['return']
                qmp('qmp_capabilities')
                def wait_for(text, start=0, timeout=20):
                    end = time.monotonic()+timeout
                    while time.monotonic() < end:
                        data = log.read_bytes()[start:]
                        if b'PANIC:' in data or b'BOOT ERROR:' in data: raise AssertionError(data)
                        if text in data: return data
                        if proc.poll() is not None: raise RuntimeError('Guest exited early')
                        time.sleep(.05)
                    raise AssertionError(f'Timeout waiting for {text!r}: {log.read_bytes()[-600:]!r}')
                wait_for(b'SHELL READY')
                assert b'KEYBOARD READY' in log.read_bytes()
                assert b'STARTUP SELFTEST PASS' in log.read_bytes(),log.read_bytes()[-1300:]
                def type_text(text):
                    mapping = {' ': 'spc', '\n': 'ret', '\b': 'backspace','.':'dot',
                               ';':'semicolon','[':'bracket_left',']':'bracket_right',
                               '-':'minus','=':'equal',',':'comma','/':'slash',
                               "'":'apostrophe','\\':'backslash','`':'grave_accent'}
                    shifted={'(':'9',')':'0','{':'bracket_left','}':'bracket_right',
                             '_':'minus','!':'1','#':'3','<':'comma','>':'dot','+':'equal',
                             '"':'apostrophe',':':'semicolon','$':'4','%':'5','^':'6',
                             '&':'7','*':'8','?':'slash','|':'backslash','~':'grave_accent'}
                    def key_event(key,down):
                        qmp('input-send-event',{'events':[{'type':'key','data':{
                            'down':down,'key':{'type':'qcode','data':key}}}]});time.sleep(.015)
                    for char in text:
                        shift=char in shifted or char.isupper()
                        key = shifted.get(char,mapping.get(char,char.lower()))
                        if shift:key_event('shift',True)
                        for down in (True, False):
                            qmp('input-send-event', {'events': [{'type':'key', 'data':{
                                'down':down, 'key':{'type':'qcode','data':key}}}]})
                            time.sleep(.015)
                        if shift:key_event('shift',False)
                def command(text, expected, timeout=20):
                    start = log.stat().st_size
                    type_text(text+'\n')
                    wait_for(expected, start, timeout)
                    wait_for(b'\r\n> ', start, timeout)
                    return log.read_bytes()[start:]
                def success(message):
                    (target/'shell-test.json').write_text(json.dumps({'passed':True,'scenario':sys.argv[1:]})+'\n')
                    print(message)
                if '--ai' in sys.argv:
                    assert b'AI DEMO MODULES AVAILABLE' in log.read_bytes()
                    cases=json.loads((ROOT/'build/ai-host/oracle-result.json').read_text())['cases']
                    command('write sentinel ai cannot change this',b'WROTE 21 BYTES')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    def infer(prompt,index):
                        data=command('ai-trace '+prompt,b'AI DEMO COMPLETE',timeout=60)
                        trace=re.search(rb'AI TOKENS:([^\r\n]+)',data)
                        assert trace and [int(x) for x in trace[1].split()]==cases[index]['trace'],data
                        assert cases[index]['text'].encode().replace(b'\n',b'\r\n') in data,data
                        return data
                    infer('Once upon a time',1)
                    warmed=command('mem',b'HEAP FREE BYTES')
                    infer('Once upon a time',1)
                    repeated=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',warmed)[1]==re.search(rb'PAGES FREE (\d+)',repeated)[1]
                    command('ai-fault',b'AI FAULT ARMED')
                    failed=command('ai test',b'AI SERVICE STOPPED - KERNEL ALIVE')
                    assert b'TASK 0 FAULT 14 CONTAINED' in failed
                    infer('The cat',2)
                    command('ai-reset',b'AI RUNTIME RESET')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1],(baseline,final)
                    command('cat sentinel',b'ai cannot change this')
                    success('PASS: real offline inference matches host tokens, warm cache, rights, contained fault/restart and complete cleanup')
                    return
                if '--sdk-auto-first' in sys.argv:
                    assert b'SDK INSTALLED 21 FILES' in log.read_bytes()
                    command('stat libnos.a',b'BYTES');command('stat stdio.h',b'BYTES')
                    success('PASS: configured bootstrap installed the SDK inside the guest')
                    return
                if '--sdk-auto-read' in sys.argv:
                    assert b'SDK AVAILABLE' in log.read_bytes()
                    assert b'SDK INSTALLED' not in log.read_bytes()
                    command('stat libnos.a',b'BYTES')
                    success('PASS: SDK bootstrap skips existing files after cold boot')
                    return
                if '--supervisor' in sys.argv:
                    command('write durable safe data',b'WROTE 9 BYTES')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    recovered=command('recoverytest',b'SERVICE RECOVERY PASS - KERNEL ALIVE')
                    assert b'FILESYSTEM SERVICE RECOVERED' in recovered
                    io=command('iofaulttest',b'IO SERVICE RECOVERY PASS - DATA INTACT',timeout=60)
                    assert b'TASK 0 FAULT 14 CONTAINED' in io and b'FILESYSTEM SERVICE RECOVERED' in io
                    command('install filedemo.nos',b'INSTALLED filedemo.nos')
                    command('runfs filedemo.nos protocol',b'C PROTOCOL DEFENSE PASS')
                    stale=command('runfs filedemo.nos backlog',b'FILESYSTEM SERVICE RECOVERED')
                    assert b'FILESYSTEM CLIENT SESSION DISCARDED' in stale
                    command('cat durable',b'safe data\r\n')
                    limited=command('recoverytest',b'FILESYSTEM RESTART LIMIT - USE restartfs')
                    assert b'SERVICE RECOVERY FAIL' in limited
                    unavailable=command('cat durable',b'ERROR: filesystem service unavailable')
                    assert b'AUTO RESTART ATTEMPT' not in unavailable
                    command('restartfs',b'FILESYSTEM SERVICE RESTARTED')
                    command('cat durable',b'safe data\r\n')
                    command('rm filedemo.nos',b'OK')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: supervisor, real mid-PIO fault/reset, malformed IPC, stale sessions, restart limit and no leaks')
                    return
                if '--supervisor-read' in sys.argv:
                    command('cat durable',b'safe data\r\n')
                    success('PASS: supervised recovery retained committed data after cold boot')
                    return
                if '--fs3-write' in sys.argv:
                    assert b'PERSISTENT FS3 FILE SERVICE READY' in log.read_bytes()
                    assert b'STORAGE LEGACY IMPORTED INTO V3' in log.read_bytes()
                    command('cat legacy',b'old v2 data preserved\r\n')
                    command('run oldprog.nos',b'EXIT CODE 42')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('fs3test write',b'EXIT CODE 0',timeout=60)
                    assert b'FS3 LARGE FILE AND DIRECTORY PASS' in result
                    command('stat big.bin',b'1048576 BYTES')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: real FS3 service imported v2, kept old NOS, wrote 1 MiB and >8 files')
                    return
                if '--fs3-read' in sys.argv:
                    assert b'PERSISTENT FS3 FILE SERVICE READY' in log.read_bytes()
                    assert b'STORAGE LEGACY IMPORTED INTO V3' not in log.read_bytes()
                    command('cat legacy',b'old v2 data preserved\r\n')
                    result=command('fs3test read',b'EXIT CODE 0',timeout=60)
                    assert b'FS3 COLD READ PASS' in result
                    command('recoverytest',b'SERVICE RECOVERY PASS - KERNEL ALIVE',timeout=60)
                    command('fs3test read',b'FS3 COLD READ PASS',timeout=60)
                    success('PASS: FS3 cold boot, 1 MiB CRC/read, pending-write crash and service recovery')
                    return
                if '--fs3-corrupt' in sys.argv:
                    assert b'FS3 STORAGE CORRUPT - WRITES DISABLED' in log.read_bytes()
                    assert b'STORAGE LEGACY IMPORTED INTO V3' not in log.read_bytes()
                    success('PASS: corrupt FS3 roots never fall back to stale v2 or format the disk')
                    return
                if '--file-sdk' in sys.argv:
                    command('rm filedemo.nos',b'> ')
                    command('install filedemo.nos',b'INSTALLED filedemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('run filedemo.nos',b'EXIT CODE 13')
                    assert b'FILE ACCESS DENIED OR FAILED' in result
                    command('runfs filedemo.nos',b'C FILE LIBRARY PASS')
                    command('cat app.txt',b'written by C\r\n')
                    command('runfs filedemo.nos crash',b'PROGRAM TERMINATED - KERNEL ALIVE')
                    command('cat app.txt',b'written by C\r\n')
                    command('runfs filedemo.nos',b'C FILE LIBRARY PASS')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm filedemo.nos',b'OK');command('rm app.txt',b'OK')
                    success('PASS: C file SDK, explicit permissions, client crash rollback and no leaks')
                    return
                if '--user-heap' in sys.argv:
                    command('rm filedemo.nos',b'> ')
                    command('install filedemo.nos',b'INSTALLED filedemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    for _ in range(2):
                        result=command('run filedemo.nos heap',b'EXIT CODE 0')
                        assert b'USER HEAP TEST PASS' in result
                    for mode in ('nx','shrink','die'):
                        result=command('run filedemo.nos '+mode,b'PROGRAM TERMINATED - KERNEL ALIVE')
                        assert b'FAULT 14 CONTAINED' in result
                    command('run filedemo.nos heap',b'USER HEAP TEST PASS')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm filedemo.nos',b'OK')
                    success('PASS: user malloc/calloc/realloc/free, 2 MiB quota, NX, unmap, zeroing and crash cleanup')
                    return
                if '--stdio' in sys.argv:
                    command('rm filedemo.nos',b'> ')
                    command('install filedemo.nos',b'INSTALLED filedemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('runfs filedemo.nos test',b'EXIT CODE 0')
                    assert b'C STREAM TEST PASS' in result
                    command('cat stream.txt',b'ABxyEF++\r\n')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm stream.txt',b'OK');command('rm filedemo.nos',b'OK')
                    success('PASS: C streams, seek, append, item counts, EOF, pushback, errors and no leaks')
                    return
                if '--descriptors' in sys.argv:
                    command('rm filedemo.nos',b'> ')
                    command('install filedemo.nos',b'INSTALLED filedemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('runfs filedemo.nos fd',b'EXIT CODE 0')
                    assert b'C DESCRIPTOR TEST PASS' in result
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm filedemo.nos',b'OK')
                    success('PASS: open/read/write/lseek/close, fdopen, append, handle exhaustion, paths and no leaks')
                    return
                if '--floats' in sys.argv:
                    command('rm floatdemo.nos',b'> ')
                    command('install floatdemo.nos',b'INSTALLED floatdemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('run floatdemo.nos',b'EXIT CODE 0')
                    assert b'C FLOAT TEST PASS' in result
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm floatdemo.nos',b'OK')
                    success('PASS: decimal/hex float parsing, exact rounding cases, limits, NaN, signed zero and no leaks')
                    return
                if '--compiler' in sys.argv:
                    source='\n'.join(['struct Pair { int a; int b; };',
                        'int values[] = {1,2,3,4};','double scale = 1.25;',
                        'static int sum(int n) {','    int result=0;',
                        '    for (int i=0;i<n;i++) result+=values[i];','    return result;','}',
                        'int main(void) {','    struct Pair p={sum(4),32};',
                        '    double x=scale*2.0;','    if (x != 2.5) return 1;',
                        '    return p.a+p.b;','}',''])
                    start=log.stat().st_size;type_text('edit native.c\n');wait_for(b'LINE EDITOR',start)
                    type_text('.clear\n'+source+'.save\n');wait_for(f'SAVED {len(source)} BYTES'.encode(),start)
                    command('write cc-0.tmp preserved',b'WROTE 9 BYTES')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('cc native.c native.nos',b'EXIT CODE 0')
                    assert b'COMPILED native.c -> native.nos' in result
                    command('run native.nos',b'EXIT CODE 42')
                    command('cat cc-0.tmp',b'preserved\r\n')
                    command('write bad.c int main( {',b'WROTE 11 BYTES')
                    bad=command('cc bad.c native.nos',b'EXIT CODE 1');assert b'error:' in bad
                    command('run native.nos',b'EXIT CODE 42')
                    command('write bad.c int missing(void); int main() { return missing(); }',b'WROTE 51 BYTES')
                    command('cc bad.c native.nos',b'EXIT CODE 1')
                    command('run native.nos',b'EXIT CODE 42')
                    command('write bad.c int huge[200000]; int main() { return 42; }',b'WROTE 43 BYTES')
                    command('cc bad.c native.nos',b'CC: executable exceeds current loader limits')
                    command('run native.nos',b'EXIT CODE 42')
                    command('cc absent.c native.nos',b'EXIT CODE 1')
                    command('cc native.c invalid.exe',b'EXIT CODE 2')
                    command('rm bad.c',b'OK');command('rm cc-0.tmp',b'OK')
                    asm='__asm__(".text\\n.globl answer\\nanswer:\\nmov $42,%eax\\nret\\n");\nint answer(void);\nint main(void) { return answer(); }\n'
                    start=log.stat().st_size;type_text('edit asm.c\n');wait_for(b'LINE EDITOR',start)
                    type_text('.clear\n'+asm+'.save\n');wait_for(f'SAVED {len(asm)} BYTES'.encode(),start)
                    command('cc asm.c asm.nos',b'EXIT CODE 0')
                    command('run asm.nos',b'EXIT CODE 42')
                    raw='.text\n.globl main\nmain:\n    mov $42,%eax\n    ret\n'
                    start=log.stat().st_size;type_text('edit raw.s\n');wait_for(b'LINE EDITOR',start)
                    type_text('.clear\n'+raw+'.save\n');wait_for(f'SAVED {len(raw)} BYTES'.encode(),start)
                    command('cc raw.s raw.nos',b'EXIT CODE 0');command('run raw.nos',b'EXIT CODE 42')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: native C/ASM compilation, globals/double/structs, errors preserve output and no leaks')
                    return
                if '--compiler-read' in sys.argv:
                    command('run native.nos',b'EXIT CODE 42');command('run asm.nos',b'EXIT CODE 42')
                    command('run raw.nos',b'EXIT CODE 42')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    command('cc native.c second.nos',b'EXIT CODE 0');command('run second.nos',b'EXIT CODE 42')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: guest-compiled C/ASM programs survive cold boot; compiler works after restart')
                    return
                if '--compiler-installed' in sys.argv:
                    baseline=command('mem',b'HEAP FREE BYTES')
                    command('install compiler.nos',b'INSTALLED compiler.nos',timeout=60)
                    size=command('stat compiler.nos',b'BYTES')
                    assert int(re.search(rb'(\d+) BYTES',size)[1])>16384
                    source='int main(void) { return 73; }'
                    command('write installed.c '+source,f'WROTE {len(source)} BYTES'.encode())
                    command('runfs compiler.nos installed.c installed.nos',b'EXIT CODE 0',timeout=60)
                    command('run installed.nos',b'EXIT CODE 73')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: >256 KiB compiler installed, loaded from filesystem and compiled a working NOS')
                    return
                if '--compiler-installed-read' in sys.argv:
                    command('run installed.nos',b'EXIT CODE 73')
                    command('runfs compiler.nos installed.c installed2.nos',b'EXIT CODE 0',timeout=60)
                    command('run installed2.nos',b'EXIT CODE 73')
                    success('PASS: installed compiler and its output work after cold restart')
                    return
                if '--sdk' in sys.argv:
                    command('write stdio.h protected header',b'WROTE 16 BYTES')
                    command('install-sdk',b'ERROR: SDK file already exists: stdio.h',timeout=60)
                    command('cat stdio.h',b'protected header\r\n')
                    command('stat libnos.a',b'ERROR: filesystem code 2')
                    command('rm stdio.h',b'OK')
                    command('install-sdk',b'SDK INSTALLED 21 FILES',timeout=60)
                    source='\n'.join(['#include <stdio.h>','#include <stdlib.h>',
                        '#include <string.h>','#include <neuro.h>',
                        'int main(void) {','    char *p=malloc(64);','    if (!p) return 1;',
                        '    strcpy(p,"hello from native C");',
                        '    printf("SDK: %s %d\\n",p,42);',
                        '    if (nos_file_write("sdk.txt",p,strlen(p))<0) return 2;',
                        '    char b[64];',
                        '    if (nos_file_read("sdk.txt",0,b,strlen(p))!=strlen(p)) return 3;',
                        '    b[strlen(p)]=0;','    if (strcmp(p,b)) return 4;','    free(p);',
                        '    double d=strtod("1.25",0);','    if (d!=1.25) return 5;',
                        '    return 42;','}',''])
                    start=log.stat().st_size;type_text('edit sdkdemo.c\n');wait_for(b'LINE EDITOR',start)
                    type_text('.clear\n'+source+'.save\n');wait_for(f'SAVED {len(source)} BYTES'.encode(),start)
                    baseline=command('mem',b'HEAP FREE BYTES')
                    command('cc sdkdemo.c sdkdemo.nos',b'EXIT CODE 0',timeout=60)
                    denied=command('run sdkdemo.nos',b'EXIT CODE 2')
                    assert b'SDK: hello from native C 42' in denied
                    result=command('runfs sdkdemo.nos',b'EXIT CODE 42')
                    assert b'SDK: hello from native C 42' in result
                    command('cat sdk.txt',b'hello from native C\r\n')
                    command('install compiler.nos',b'INSTALLED compiler.nos',timeout=60)
                    command('runfs compiler.nos sdkdemo.c sdkdisk.nos',b'EXIT CODE 0',timeout=60)
                    command('runfs sdkdisk.nos',b'EXIT CODE 42')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: atomic SDK install, protected existing header, native headers/archive/printf/malloc/strtod/files')
                    return
                if '--sdk-read' in sys.argv:
                    command('runfs sdkdemo.nos',b'EXIT CODE 42')
                    command('runfs compiler.nos sdkdemo.c sdkagain.nos',b'EXIT CODE 0',timeout=60)
                    command('runfs sdkagain.nos',b'EXIT CODE 42')
                    success('PASS: SDK, native linked executable and fresh compilation survive cold restart')
                    return
                if '--libc' in sys.argv:
                    command('rm libcdemo.nos',b'> ')
                    command('install libcdemo.nos',b'INSTALLED libcdemo.nos')
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('runfs libcdemo.nos',b'EXIT CODE 0')
                    assert b'C LIBRARY TEST PASS' in result
                    command('stat format.txt',b'300 BYTES')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    command('rm format.txt',b'OK');command('rm libcdemo.nos',b'OK')
                    success('PASS: C formatting, strings, nested longjmp, all callee-saved registers and no leaks')
                    return
                if '--corrupt-check' in sys.argv:
                    data=log.read_bytes()
                    assert b'STORAGE CORRUPT - WRITES DISABLED' in data
                    assert b'FILE SERVICE INITIALIZATION FAILED' in data
                    assert b'STORAGE V1 IMPORTED' not in data
                    success('PASS: both v2 banks corrupt - no stale v1 fallback or formatting')
                    return
                if '--migrate' in sys.argv:
                    command('cat legacy',b'preserved from v1\r\n')
                    command('write upgraded new format alive',b'WROTE 16 BYTES')
                    command('cat legacy',b'preserved from v1\r\n')
                    assert b'STORAGE V1 IMPORTED' in log.read_bytes()
                    success('PASS: v1 data imported and committed to v2')
                    return
                if '--migration-read' in sys.argv:
                    command('cat legacy',b'preserved from v1\r\n')
                    command('cat upgraded',b'new format alive\r\n')
                    command('run big.nos larger',b'EXIT CODE 42')
                    assert b'STORAGE V1 IMPORTED' not in log.read_bytes()
                    success('PASS: migrated data and 3 KiB NOS survive cold restart')
                    return
                if '--editor-write' in sys.argv:
                    start=log.stat().st_size;type_text('edit source.c\n')
                    wait_for(b'LINE EDITOR - APPEND MODE',start)
                    type_text('.clear\nint main() {\n    return 42;\n}\n.save\n')
                    wait_for(b'SAVED 30 BYTES',start)
                    command('cat source.c',b'int main() {\r\n    return 42;\r\n}\r\n')
                    start=log.stat().st_size;type_text('edit source.c\n');wait_for(b'LINE EDITOR',start)
                    type_text('.clear\nnot saved\n.cancel\n');wait_for(b'EDITOR CANCELLED',start)
                    command('cat source.c',b'int main() {\r\n    return 42;\r\n}\r\n')
                    command('echo AbC_09!',b'AbC_09!\r\n')
                    success('PASS: C punctuation, multiline editing, save and cancellation')
                    return
                if '--editor-read' in sys.argv:
                    command('cat source.c',b'int main() {\r\n    return 42;\r\n}\r\n')
                    qmp('screendump',{'filename':str(target/'editor.ppm')})
                    command('rm source.c',b'OK')
                    success('PASS: source code survived cold restart')
                    return
                if '--nos-write' in sys.argv:
                    command('rm hello.nos',b'> ')
                    command('install hello.nos',b'INSTALLED hello.nos')
                    result=command('run hello.nos first',b'EXIT CODE 42')
                    assert b'HELLO FROM A NOS PROGRAM' in result and b'first' in result
                    success('PASS: installed and ran a C NOS program from persistent storage')
                    return
                if '--nos-read' in sys.argv:
                    baseline=command('mem',b'HEAP FREE BYTES')
                    result=command('run hello.nos reboot',b'EXIT CODE 42')
                    assert b'ARGUMENT: reboot' in result
                    command('write broken.nos invalid',b'WROTE 7 BYTES')
                    command('run broken.nos',b'ERROR: invalid or unsupported NOS executable')
                    command('rm broken.nos',b'OK')
                    command('rm hello.nos',b'OK')
                    final=command('mem',b'HEAP FREE BYTES')
                    assert re.search(rb'PAGES FREE (\d+)',baseline)[1]==re.search(rb'PAGES FREE (\d+)',final)[1]
                    success('PASS: NOS executable survived reboot; malformed executable rejected')
                    return
                if '--persist-write' in sys.argv:
                    command('write durable saved across reboot',b'WROTE 19 BYTES')
                    command('cat durable',b'saved across reboot\r\n')
                    command('write marker x',b'WROTE 1 BYTES')
                    assert b'PERSISTENT FILE SERVICE READY' in log.read_bytes()
                    success('PASS: file committed to virtual disk')
                    return
                if '--persist-read' in sys.argv:
                    command('cat durable',b'saved across reboot\r\n')
                    command('rm durable',b'OK')
                    success('PASS: file recovered after cold guest restart')
                    return
                command('help', b'alloc PAGES')
                command('info', f'NeuroOS {neuro.VERSION}'.encode())
                initial = command('mem', b'HEAP FREE BYTES')
                command('alloc 2', b'ALLOCATED ID 0')
                command('heap 33', b'ALLOCATED ID 1')
                allocated = command('mem', b'HEAP FREE BYTES')
                first_free = int(re.search(rb'PAGES FREE (\d+)', initial)[1])
                after_free = int(re.search(rb'PAGES FREE (\d+)', allocated)[1])
                assert first_free-after_free == 2
                command('free 0', b'FREED')
                command('free 1', b'FREED')
                command('free 1', b'ERROR: invalid allocation ID')
                final = command('mem', b'HEAP FREE BYTES')
                assert re.search(rb'PAGES FREE (\d+)', final)[1] == str(first_free).encode()
                assert re.search(rb'HEAP FREE BYTES (\d+)', initial)[1] == re.search(rb'HEAP FREE BYTES (\d+)', final)[1]
                command('test', b'SELFTEST PASS')
                for _ in range(2):
                    result = command('proctest', b'PROCESS TEST PASS')
                    assert b'IPC RECEIVED AND PEER FAILURE SURVIVED' in result
                    assert b'TASK 2 CPU BUDGET EXPIRED' in result
                    assert b'PROCESS TEST FAIL' not in result
                after_processes = command('mem', b'HEAP FREE BYTES')
                assert re.search(rb'PAGES FREE (\d+)', after_processes)[1] == str(first_free).encode()
                assert re.search(rb'HEAP FREE BYTES (\d+)', after_processes)[1] == re.search(rb'HEAP FREE BYTES (\d+)', initial)[1]
                command('fstest',b'SERVICE TEST PASS')
                command('touch note',b'OK')
                command('write note hello from the microkernel',b'WROTE 26 BYTES')
                command('cat note',b'hello from the microkernel\r\n')
                command('recoverytest',b'SERVICE RECOVERY PASS - KERNEL ALIVE')
                command('cat note',b'hello from the microkernel\r\n')
                command('stat note',b'26 BYTES')
                command('ls',b'note 26 BYTES')
                command('proctest',b'PROCESS TEST PASS')
                command('cat note',b'hello from the microkernel\r\n')
                command('write note short',b'WROTE 5 BYTES')
                command('cat note',b'short\r\n')
                command('write long abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz',b'WROTE 52 BYTES')
                command('cat long',b'abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\r\n')
                command('rm note',b'OK')
                command('rm long',b'OK')
                command('cat note',b'ERROR: filesystem code 2')
                after_files=command('mem',b'HEAP FREE BYTES')
                assert re.search(rb'PAGES FREE (\d+)',after_files)[1]==str(first_free).encode()
                uptime1 = command('uptime', b'TICKS ')
                time.sleep(.1)
                uptime2 = command('uptime', b'TICKS ')
                assert int(re.search(rb'TICKS (\d+)', uptime2)[1]) > int(re.search(rb'TICKS (\d+)', uptime1)[1])
                command('unknown', b'ERROR: unknown command')
                command('echo ax\bb', b'ab\r\n')
                command('alloc 18446744073709551616', b'ERROR: positive size required')
                command('x'*100, b'ERROR: line too long')
                command('clear', b'> ')
                command('help', b'alloc PAGES')
                command('mem', b'HEAP FREE BYTES')
                command('test', b'SELFTEST PASS')
                qmp('screendump', {'filename':str(target/'shell.ppm')})
                (target/'shell-test.json').write_text(json.dumps({'passed':True,
                    'input':'QMP keyboard events -> PS/2 -> kernel shell',
                    'checks':'commands, memory accounting, timer, backspace, overflow, display'}, indent=2)+'\n')
                success('PASS: production shell keyboard, commands, timer and memory accounting')
        finally:
            if proc.poll() is None: proc.kill()
            proc.wait()


if __name__ == '__main__': main()
