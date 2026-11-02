"""Compile-only dependency probe. Never claims to build a working guest compiler.

The caller supplies a pinned local source checkout; no downloads, host libraries,
or fake successful implementations are used. Upstream is not modified.
"""
import argparse
import difflib
import json
from pathlib import Path
import shutil
import subprocess
import neuro

COMMIT='e23dabed154a8495f3ac1ff3cc9d04fa1c207893'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    args=parser.parse_args()
    source=args.source.resolve()
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    if revision!=COMMIT:raise RuntimeError('Source revision does not match the reviewed pin')
    if subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=source):
        raise RuntimeError('Source checkout has local modifications')
    target=neuro.ROOT/'build/compiler-probe';target.mkdir(parents=True,exist_ok=True)
    report={'source':str(source),'commit':revision,'object_built':False,
            'guest_compiler_ready':False,'stage':'compiling'}
    (target/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    copied=target/'source'
    shutil.copytree(source,copied,ignore=shutil.ignore_patterns('.git'),dirs_exist_ok=True)
    path=copied/'tcc.h';old=path.read_text()
    needle='\n#if defined CONFIG_TCC_BACKTRACE'
    if old.count(needle)!=1:raise RuntimeError('Upstream native guard changed')
    new=old.replace(needle,'\n#ifdef __neuroos__\n# undef TCC_IS_NATIVE /* No JIT, host dlopen or tcc -run. */\n#endif\n'+needle)
    new=new.replace('#elif !(defined TCC_TARGET_PE || defined TCC_TARGET_MACHO)',
                    '#elif !defined __neuroos__ && !(defined TCC_TARGET_PE || defined TCC_TARGET_MACHO)')
    path.write_text(new)
    (target/'adaptation.diff').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/tcc.h',tofile='b/tcc.h')))
    (copied/'config.h').write_text('''#define TCC_VERSION "0.9.28-neuroos-probe"
#define TCC_TARGET_X86_64 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_PREDEFS 0
#define CONFIG_TCCDIR ""
#define CONFIG_TCC_SYSINCLUDEPATHS ""
#define CONFIG_TCC_LIBPATHS ""
#define CONFIG_TCC_CRTPREFIX ""
''')
    clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
    command=[clang,'--target=x86_64-unknown-none-elf','-std=gnu11','-Os',
             '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie',
             '-mno-red-zone','-mno-avx','-ffunction-sections','-fdata-sections',
             '-D__neuroos__=1','-I'+str(neuro.ROOT/'ports/tinycc/include'),
             '-I'+str(neuro.ROOT/'user/libc'),'-ferror-limit=0',
             '-c',str(copied/'libtcc.c'),'-o',str(target/'libtcc.o')]
    result=subprocess.run(command,cwd=target,text=True,capture_output=True)
    (target/'compile.log').write_text(result.stdout+result.stderr)
    report.update(compile_exit=result.returncode,command=command)
    if result.returncode:
        report['stage']='compile-errors'
        (target/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(result.stderr[-12000:]);raise SystemExit(result.returncode)
    nm=neuro.tool('LLVM_NM','llvm-nm',[r'C:\Program Files\LLVM\bin\llvm-nm.exe'])
    missing=subprocess.check_output([nm,'--undefined-only',str(target/'libtcc.o')],text=True)
    sizes=subprocess.check_output([neuro.tool('LLVM_SIZE','llvm-size',[r'C:\Program Files\LLVM\bin\llvm-size.exe']),str(target/'libtcc.o')],text=True)
    report.update(object_built=True,stage='dependencies-measured',
                  external_symbols=[line.split()[-1] for line in missing.splitlines() if line.strip()],
                  object_bytes=(target/'libtcc.o').stat().st_size,section_sizes=sizes)
    (target/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(sizes);print(missing)
    print('Compile-only probe complete; guest compiler is NOT ready. Report:',target/'result.json')

if __name__=='__main__':main()
