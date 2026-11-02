"""Prepare the pinned upstream compiler without modifying its vendored source."""
import difflib
import json
from pathlib import Path
import re
import shutil

def prepare(source:Path,target:Path):
    target.mkdir(parents=True,exist_ok=True)
    shutil.copytree(source,target,dirs_exist_ok=True)
    patches=[]
    def change(name,old,new,expected=1):
        path=target/name;text=path.read_text()
        if text.count(old)!=expected:raise RuntimeError(f'Upstream patch context changed: {name}: {old[:70]}')
        updated=text.replace(old,new)
        patches.extend(difflib.unified_diff(text.splitlines(True),updated.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
        path.write_text(updated)
    change('tcc.h','\n#if defined CONFIG_TCC_BACKTRACE',
           '\n#ifdef __neuroos__\n# undef TCC_IS_NATIVE /* No JIT or host dynamic loader. */\n#endif\n\n#if defined CONFIG_TCC_BACKTRACE')
    change('tcc.h','#elif !(defined TCC_TARGET_PE || defined TCC_TARGET_MACHO)',
           '#elif !defined __neuroos__ && !(defined TCC_TARGET_PE || defined TCC_TARGET_MACHO)')
    change('tccpp.c','static const char * const target_os_defs =\n#ifdef TCC_TARGET_PE',
           'static const char * const target_os_defs =\n#ifdef __neuroos__\n    "__neuroos__\\0"\n#elif defined TCC_TARGET_PE')
    # Reproducible compiler policy until a guest wall clock is implemented.
    change('tccpp.c','''            time_t ti;
            struct tm *tm;
            time(&ti);
            tm = localtime(&ti);''','''            struct tm fixed = {.tm_year=70, .tm_mon=0, .tm_mday=1};
            struct tm *tm = &fixed; /* Reproducible epoch 1970-01-01 UTC. */''')
    # The kernel requires every PT_LOAD virtual start to be page-aligned.
    change('tccelf.c','''        if ((f & SHFX_NEWPH) && n) {
            /* different rwx section flags */''','''        if ((f & SHFX_NEWPH) && n) {
            align = s_align - 1; /* NeuroOS page-granular segment permissions. */
            /* different rwx section flags */''')
    # Our offset writes zero-fill holes. Seeking avoids committing a whole disk
    # snapshot for every padding byte through the unbuffered stream adapter.
    change('tccelf.c','''            while (offset < s->sh_offset) {
                fputc(0, f);
                offset++;
            }''','''            if (offset < s->sh_offset) {
                if (fseek(f, s->sh_offset, SEEK_SET)) return -1;
                offset = s->sh_offset;
            }''',expected=2)
    change('tccelf.c','''    fclose(f);

    return ret;
}''','''    if (ferror(f)) ret = -1;
    if (fclose(f)) ret = -1;

    return ret;
}''')
    (target/'neuroos.patch').write_text(''.join(patches))
    (target/'config.h').write_text('''#define TCC_VERSION "0.9.28-neuroos"
#define TCC_TARGET_X86_64 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCCDIR ""
#define CONFIG_TCC_SYSINCLUDEPATHS ""
#define CONFIG_TCC_LIBPATHS ""
#define CONFIG_TCC_CRTPREFIX ""
''')
    # Like upstream conftest.c C2STR, lines indented >=4 become runtime C
    # strings; column-1 platform conditions are evaluated at compiler build.
    macros={'__i386__':'TCC_TARGET_I386','__x86_64__':'TCC_TARGET_X86_64',
            '_WIN32':'TCC_TARGET_PE','__arm__':'TCC_TARGET_ARM','__ARM_EABI__':'TCC_ARM_EABI',
            '__aarch64__':'TCC_TARGET_ARM64','__riscv':'TCC_TARGET_RISCV64',
            '__APPLE__':'TCC_TARGET_MACHO','__FreeBSD__':'TARGETOS_FreeBSD',
            '__FreeBSD_kernel__':'TARGETOS_FreeBSD_kernel','__OpenBSD__':'TARGETOS_OpenBSD',
            '__NetBSD__':'TARGETOS_NetBSD','__linux__':'TARGETOS_Linux','__ANDROID__':'TARGETOS_ANDROID',
            '__SIZEOF_POINTER__':'PTR_SIZE','__SIZEOF_LONG__':'LONG_SIZE'}
    converted=[]
    for line in (source/'include/tccdefs.h').read_text().splitlines(True):
        if len(line)-len(line.lstrip(' '))>=4:
            converted.append(json.dumps(line)+'\n')
        else:
            converted.append(re.sub(r'\b\w+\b',lambda m:macros.get(m[0],m[0]),line))
    (target/'tccdefs_.h').write_text(''.join(converted))
    return target
