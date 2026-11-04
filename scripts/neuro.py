"""Offline build and UEFI boot smoke test; Python standard library only."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
_version=re.search(r'^#define NEUROOS_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"$',
                   (ROOT/'kernel/version.h').read_text(),re.MULTILINE)
if not _version:raise RuntimeError('Invalid kernel version header')
VERSION=_version.group(1)
MARKER = f'NeuroOS v{VERSION} — Kernel boot successful'.encode('utf-8')


def tool(env, name, candidates=()):
    for candidate in [os.environ.get(env), shutil.which(name), *candidates]:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise RuntimeError(f'{name} not found; set {env} to its absolute path')


def verify_vendor():
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    for name, expected in lock['limine']['sha256'].items():
        path = ROOT / 'vendor' / 'limine' / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Vendor checksum mismatch: {path}')
    for name,expected in lock['musl-floatscan']['sha256'].items():
        path=ROOT/'vendor/musl-1.2.5'/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'Vendor checksum mismatch: {path}')
    for name,expected in lock['tinycc']['sha256'].items():
        path=ROOT/'vendor/tinycc'/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'Vendor checksum mismatch: {path}')
    for name,expected in lock.get('llama2c',{}).get('sha256',{}).items():
        path=ROOT/'vendor/llama2c'/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:
            raise RuntimeError(f'Vendor checksum mismatch: {path}')


def build(test=False, fault=0):
    verify_vendor()
    backend=os.environ.get('NEUROOS_FS_BACKEND','3')
    if backend not in ('2','3'):raise RuntimeError('NEUROOS_FS_BACKEND must be 2 or 3')
    fs3=backend=='3'
    fs3_test=fs3 and os.environ.get('NEUROOS_FS3_TEST')=='1'
    bootstrap_sdk=os.environ.get('NEUROOS_BOOTSTRAP_SDK')=='1'
    desktop=os.environ.get('NEUROOS_DESKTOP')=='1'
    ai_demo=os.environ.get('NEUROOS_AI_DEMO')=='1'
    if ai_demo and fs3_test:raise RuntimeError('AI demo and FS3 stress use separate test images')
    variant = f'fault-{fault}' if fault else 'test' if test else 'normal'
    if not fs3:variant+='-fs2'
    if fs3_test:variant+='-fs3-test'
    if bootstrap_sdk:variant+='-bootstrap'
    if desktop:variant+='-desktop'
    if ai_demo:variant+='-ai'
    target = ROOT / 'build' / variant
    esp = target / 'esp'
    (esp / 'EFI' / 'BOOT').mkdir(parents=True, exist_ok=True)
    (esp / 'boot').mkdir(parents=True, exist_ok=True)
    clang = tool('CLANG', 'clang', [r'C:\Program Files\LLVM\bin\clang.exe'])
    linker = tool('LD_LLD', 'ld.lld', [r'C:\Program Files\LLVM\bin\ld.lld.exe'])
    flags = ['--target=x86_64-unknown-none-elf', '-std=c11', '-O2', '-g',
             '-ffreestanding', '-fno-stack-protector', '-fno-pic', '-fno-pie',
             '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-mno-red-zone', '-mgeneral-regs-only', '-mcmodel=kernel',
             '-Wall', '-Wextra', '-Werror', '-Ivendor/limine',
             f'-ffile-prefix-map={ROOT.as_posix()}=.']
    if test:
        flags.append('-DNEUROOS_BOOT_TEST=1')
    flags.append(f'-DNEUROOS_FAULT_TEST={fault}')
    flags.append(f'-DNEUROOS_FS3_TEST={int(fs3_test)}')
    flags.append(f'-DNEUROOS_BOOTSTRAP_SDK={int(bootstrap_sdk)}')
    flags.append(f'-DNEUROOS_DESKTOP={int(desktop)}')
    flags.append(f'-DNEUROOS_AI_DEMO={int(ai_demo)}')
    elf = esp / 'boot' / 'kernel.elf'
    objects = []
    for source in ['main.c', 'memory.c', 'exceptions.c', 'entry.S', 'runtime.c',
                   'alloc.c', 'paging.c', 'devices.c', 'timer.S', 'shell.c',
                   'process.c', 'user_program.S','elf.c','files.c','editor.c','fpu.c','sdk.c','display.c',
                   'journal.c']:
        obj = target / (Path(source).stem + '.o')
        subprocess.run([clang, *flags, '-c', 'kernel/' + source, '-o', str(obj)],
                       cwd=ROOT, check=True)
        objects.append(str(obj))
    subprocess.run([linker, '-nostdlib', '-static', '-z', 'max-page-size=4096',
                    '--build-id=none', '-T', 'kernel/linker.ld',
                    *objects, '-o', str(elf)], cwd=ROOT, check=True)
    user_flags=[f for f in flags if not f.startswith('-mcmodel=') and f!='-mgeneral-regs-only']
    user_flags+=['-mcmodel=small','-mno-avx','-fno-builtin','-Iuser']
    if ai_demo:
        for name,path in [('ai_prepare','ports/llama2c/prepare.py'),('ai_pack','scripts/make-ai-pack.py')]:
            spec=importlib.util.spec_from_file_location(name,ROOT/path)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            if name=='ai_prepare':module.prepare(ROOT,target)
            else:module.build(ROOT,target)
    apps=['ramfs','fsclient','filedemo','libcdemo','floatdemo','desktop']+(['fs3demo'] if fs3_test else [])+(['airuntime','aiclient'] if ai_demo else [])
    for app in apps:
        installable=app in ('filedemo','libcdemo','floatdemo','fs3demo')
        app_objects=[]
        sources=['user/start.S','kernel/runtime.c',f'user/{app}.c']
        if app=='ramfs': sources+=['user/fs.c','user/storage.c']
        if app=='ramfs' and fs3:
            sources.remove('user/ramfs.c');sources+=['user/ramfs3.c','user/fs3.c']
        if installable:sources+=['user/file.c','user/heap.c','user/stdio.c']
        if app=='desktop':sources+=['user/file.c','user/heap.c','user/format.c','user/string.c','user/stdio.c',
                                    'user/text.c','user/keyboard.c']
        if app=='airuntime':sources+=['user/ai_core.c','ports/llama2c/math.c','user/heap.c','user/stdio.c',
                                     'user/format.c','user/string.c','user/stdlib.c','user/file.c']
        if app=='filedemo':sources+=['tests/user_io.c']
        if app=='libcdemo':sources+=['user/format.c','user/string.c','user/setjmp.S','user/stdlib.c',
                                    'tests/user_libc.c','tests/user_jmp.S']
        if app=='floatdemo':sources+=['user/float_math.c','user/strtod.c','vendor/musl-1.2.5/floatscan.c']
        for source in sources:
            obj=target/(app+'-'+Path(source).stem+'.o')
            extra=['-Os','-ffunction-sections','-fdata-sections'] if installable else []
            if app=='airuntime':extra+=['-I'+str(target),'-Wno-sign-compare','-Wno-unknown-pragmas']
            if source=='user/strtod.c' or source.startswith('vendor/musl-'):
                extra+=['-Iports/musl/include','-Iuser/libc','-Dhidden=']
            if source.startswith('vendor/musl-'):extra+=['-Wno-parentheses','-Wno-sign-compare']
            subprocess.run([clang,*user_flags,*extra,
                            '-c',source,'-o',str(obj)],cwd=ROOT,check=True)
            app_objects.append(str(obj))
        subprocess.run([linker,'-nostdlib','-static','-z',
                        'max-page-size=16' if installable else 'max-page-size=4096',
                        '--build-id=none','-T','user/linker.ld',*app_objects,*(['-s','--gc-sections'] if installable else []),
                        '-o',str(esp/'boot'/f'{app}.nos')],cwd=ROOT,check=True)
        program=esp/'boot'/f'{app}.nos'
        content=bytearray(program.read_bytes());content[7]=0x4e;content[8]=1;program.write_bytes(content)
        if installable and len(content)>16384:
            raise RuntimeError('Installable C sample exceeds filesystem file limit')
    spec=importlib.util.spec_from_file_location('tinycc_prepare',ROOT/'ports/tinycc/prepare.py')
    preparation=importlib.util.module_from_spec(spec);spec.loader.exec_module(preparation)
    compiler_source=preparation.prepare(ROOT/'vendor/tinycc',target/'tinycc-source')
    sdk_spec=importlib.util.spec_from_file_location('make_sdk',ROOT/'scripts/make-sdk.py')
    sdk_builder=importlib.util.module_from_spec(sdk_spec);sdk_spec.loader.exec_module(sdk_builder)
    sdk_builder.build(ROOT,target,clang,user_flags,tool)
    runtime='typedef unsigned long size_t;\n'+''.join(line for line in (ROOT/'kernel/runtime.c').read_text().splitlines(True) if not line.startswith('#include'))
    (target/'compiler-runtime.h').write_text('static const char runtime_source[] = '+json.dumps(runtime)+';\n')
    compiler_objects=[]
    compiler_sources=['user/start.S','kernel/runtime.c','kernel/elf.c','user/compiler.c','user/file.c',
        'user/heap.c','user/stdio.c','user/format.c','user/string.c','user/stdlib.c','user/setjmp.S',
        'user/strtod.c','user/float_math.c','vendor/musl-1.2.5/floatscan.c',str(compiler_source/'libtcc.c')]
    for source in compiler_sources:
        obj=target/('compiler-'+Path(source).stem+'.o')
        extra=['-Os','-ffunction-sections','-fdata-sections','-I'+str(compiler_source),'-I'+str(target),
               f'-ffile-prefix-map={compiler_source.as_posix()}=vendor/tinycc']
        if source=='user/strtod.c' or source.startswith('vendor/musl-'):
            extra+=['-Iports/musl/include','-Iuser/libc','-Dhidden=']
        if source.startswith('vendor/musl-'):extra+=['-Wno-parentheses','-Wno-sign-compare']
        if source==str(compiler_source/'libtcc.c'):
            extra+=['-std=gnu11','-D__neuroos__=1','-Iports/tinycc/include','-Iuser/libc',
                    '-Wno-sign-compare','-Wno-unused-parameter','-Wno-missing-field-initializers']
        subprocess.run([clang,*user_flags,*extra,'-c',source,'-o',str(obj)],cwd=ROOT,check=True)
        compiler_objects.append(str(obj))
    program=esp/'boot/compiler.nos'
    subprocess.run([linker,'-nostdlib','-static','-z','max-page-size=4096','--build-id=none',
                    '--gc-sections','-s','-T','user/linker.ld',*compiler_objects,'-o',str(program)],cwd=ROOT,check=True)
    content=bytearray(program.read_bytes());content[7]=0x4e;content[8]=1;program.write_bytes(content)
    hello_objects=[]
    for source in ('user/start.S','user/hello.c'):
        obj=target/('hello-'+Path(source).stem+'.o')
        subprocess.run([clang,*user_flags,'-c',source,'-o',str(obj)],cwd=ROOT,check=True)
        hello_objects.append(str(obj))
    linked=target/'hello-linked.elf'
    subprocess.run([linker,'-nostdlib','-static','-T','user/hello.ld',*hello_objects,'-o',str(linked)],cwd=ROOT,check=True)
    objcopy=tool('LLVM_OBJCOPY','llvm-objcopy',[r'C:\Program Files\LLVM\bin\llvm-objcopy.exe'])
    raw=target/'hello.bin'
    subprocess.run([objcopy,'-O','binary','--only-section=.text',str(linked),str(raw)],check=True)
    code=raw.read_bytes();length=128+len(code)
    if length>1024:raise RuntimeError('Sample executable exceeds current filesystem limit')
    ident=b'\x7fELF'+bytes([2,1,1,0x4e,1])+bytes(7)
    header=struct.pack('<16sHHIQQQIHHHHHH',ident,2,62,1,0x400080,64,0,0,64,56,1,0,0,0)
    ph=struct.pack('<IIQQQQQQ',1,5,0,0x400000,0,length,length,4096)
    (esp/'boot/hello.nos').write_bytes(header+ph+bytes(8)+code)
    shutil.copyfile(ROOT / 'vendor/limine/BOOTX64.EFI', esp / 'EFI/BOOT/BOOTX64.EFI')
    shutil.copyfile(ROOT / 'boot/limine.conf', esp / 'limine.conf')
    if fs3_test:
        with (esp/'limine.conf').open('a') as config:
            config.write('    module_path: boot():/boot/fs3demo.nos\n')
    if ai_demo:
        with (esp/'limine.conf').open('a') as config:
            for name in ('airuntime.nos','aiclient.nos','ai.pack'):
                config.write('    module_path: boot():/boot/'+name+'\n')
    manifest = {
        'variant': variant,
        'filesystem_backend':backend,
        'bootstrap_sdk':bootstrap_sdk,
        'desktop':desktop,
        'ai_demo':ai_demo,
        'clang': subprocess.check_output([clang, '--version'], text=True).splitlines()[0],
        'linker': subprocess.check_output([linker, '--version'], text=True).strip(),
        'kernel_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
    }
    (target / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {elf}', flush=True)
    return target


def qemu_command(target, test, memory):
    qemu = tool('QEMU', 'qemu-system-x86_64',
                [ROOT / 'tools/qemu/qemu-system-x86_64.exe'])
    code = os.environ.get('OVMF_CODE')
    if not code:
        code = Path(qemu).parent / 'share/edk2-x86_64-code.fd'
    code = Path(code).resolve()
    if not code.is_file():
        raise RuntimeError('Set OVMF_CODE to an x86-64 OVMF/EDK II firmware image')
    variables = Path(os.environ.get('OVMF_VARS', str(code.parent / 'edk2-i386-vars.fd')))
    if not variables.is_file():
        raise RuntimeError('Set OVMF_VARS to the matching OVMF variable-store template')
    shutil.copyfile(variables, target / 'ovmf-vars.fd')
    cmd = [qemu, '-machine', 'q35', '-accel', 'tcg', '-cpu', 'qemu64',
           '-m', str(memory), '-smp', '1', '-net', 'none',
           '-no-reboot', '-monitor', 'none',
           '-drive', f'if=pflash,format=raw,readonly=on,file={str(code).replace(",", ",,")}',
           '-drive', 'if=pflash,format=raw,file=ovmf-vars.fd',
           '-drive', 'if=none,id=esp,format=raw,readonly=on,file=fat:ro:esp',
           '-device', 'virtio-blk-pci,drive=esp',
           '-serial', 'file:serial.log', '-display', 'none']
    if test:
        cmd += ['-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04']
    else:
        disk=Path(os.environ.get('NEUROOS_DATA_DISK',str(target/'data.img'))).resolve()
        if not disk.exists():
            with disk.open('xb') as image:image.truncate(64*1024*1024)
        cmd+=['-device','piix3-ide,id=dataide',
              '-drive','if=none,id=userdata,format=raw,file='+str(disk).replace(',',',,'),
              '-device','ide-hd,drive=userdata,bus=dataide.0,unit=0']
    return cmd


def boot(target, test, timeout, memory, fault=0):
    cmd = qemu_command(target, test, memory)
    # Fresh log on every run: never pass because of a previous boot.
    log = target / 'serial.log'
    log.write_bytes(b'')
    result_path = target / 'test-result.json'
    if test:
        result_path.write_text(json.dumps({'passed': False, 'status': 'running'}) + '\n')
    print('Starting UEFI guest (headless; Ctrl+C stops run)...', flush=True)
    with (target / 'qemu.stdout.log').open('wb') as stdout, \
            (target / 'qemu.stderr.log').open('wb') as stderr:
        process = subprocess.Popen(cmd, cwd=target, stdout=stdout, stderr=stderr)
        try:
            code = process.wait(timeout=timeout if test else None)
        except subprocess.TimeoutExpired:
            result_path.write_text(json.dumps({'passed': False, 'status': 'timeout',
                                               'timeout_seconds': timeout}) + '\n')
            raise RuntimeError(f'Boot timed out after {timeout}s; see {target}')
        except KeyboardInterrupt:
            print('Guest stopped.')
            return
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    data = log.read_bytes()
    # Firmware uses terminal control sequences; keep the raw bytes in serial.log.
    for line in data.splitlines():
        if line.startswith((b'NeuroOS ', b'Firmware:', b'BOOT ERROR:', b'CPU:',
                            b'MEMORY:', b'PANIC:', b'VECTOR=', b'ERROR=', b'RIP=',
                            b'DF IST:', b'SELFTEST:', b'ALLOC:', b'PAGING:',
                            b'PROCESS TEST', b'TASK ', b'USER ', b'IPC SENT',b'SERVICE')):
            print(line.decode('utf-8', errors='replace'), flush=True)
    if test:
        mem = re.search(rb'MEMORY: usable_bytes=(\d+) regions=(\d+)', data)
        memory_valid = bool(mem and 0 < int(mem[1]) <= memory * 1024 * 1024
                            and int(mem[2]) > 0)
        passed = (code == (37 if fault else 33) and MARKER in data
                  and b'Firmware: UEFI64' in data and memory_valid
                  and b'CPU: GDT TSS IDT ready' in data and b'BOOT ERROR:' not in data)
        passed = passed and b'ALLOC: physical pages and heap PASS' in data and b'PAGING: own tables and mapping test PASS' in data
        if fault:
            passed = passed and b'SELFTEST: exception PASS' in data and (
                f'VECTOR=0x{fault:016x}'.encode() in data)
        else:
            passed = passed and b'PANIC:' not in data and b'PROCESS TEST PASS' in data and b'SERVICE TEST PASS' in data
        result = {'passed': passed, 'exit_code': code, 'memory_mib': memory,
                  'serial_log': str(log), 'command': cmd}
        (target / 'test-result.json').write_text(json.dumps(result, indent=2) + '\n')
        if not passed:
            raise RuntimeError(f'Boot failed (exit {code}); see {target}')
        print(f'PASS: boot, memory map, CPU tables, expected exit {code}; fault={fault}')
    elif code:
        raise RuntimeError(f'QEMU exited with {code}; see {target}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'test', 'run'])
    parser.add_argument('--timeout', type=int, default=45)
    parser.add_argument('--memory', type=int, default=256)
    parser.add_argument('--fault', type=int, choices=[0, 6, 13, 8, 14], default=0,
                        help='test only: invalid opcode, general protection, double fault')
    args = parser.parse_args()
    if args.timeout <= 0 or args.memory < 64:
        parser.error('timeout must be positive; memory must be at least 64 MiB')
    if args.fault and args.action != 'test':
        parser.error('--fault is only valid with test')
    target = build(test=args.action == 'test', fault=args.fault)
    if args.action != 'build':
        boot(target, args.action == 'test', args.timeout, args.memory, args.fault)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
