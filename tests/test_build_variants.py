"""Verify tested user payloads are identical in the bootstrap ISO configuration.

Compiler assertions contain __FILE__; generated source paths must not silently
change its executable when only the build directory or bootstrap flag changes.
"""
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro

def loads(path):
    data = path.read_bytes()
    assert data[:4] == b'\x7fELF'
    offset = struct.unpack_from('<Q', data, 32)[0]
    step, count = struct.unpack_from('<HH', data, 54)
    segments = []
    for index in range(count):
        kind, flags, start, address, _, size, memory, _ = struct.unpack_from(
            '<II6Q', data, offset + index * step)
        if kind == 1:
            segments.append((flags, address, memory, data[start:start + size]))
    assert segments
    return segments

folder = ROOT / 'build/build-variants'
folder.mkdir(parents=True, exist_ok=True)
report = folder / 'result.json'
report.write_text('{"passed":false,"status":"running"}\n')
os.environ.pop('NEUROOS_FS3_TEST', None)
os.environ.update(NEUROOS_AI_DEMO='0', NEUROOS_DESKTOP='0', NEUROOS_BOOTSTRAP_SDK='0')
ordinary = neuro.build()
os.environ.update(NEUROOS_AI_DEMO='1', NEUROOS_DESKTOP='1')
os.environ['NEUROOS_BOOTSTRAP_SDK'] = '0'
tested = neuro.build()
os.environ['NEUROOS_BOOTSTRAP_SDK'] = '1'
production = neuro.build()
apps = ('ramfs', 'fsclient', 'hello', 'filedemo', 'libcdemo', 'floatdemo',
        'compiler', 'desktop', 'airuntime', 'aiclient')
for name in apps:
    assert loads(tested / f'esp/boot/{name}.nos') == loads(production / f'esp/boot/{name}.nos'), name
assert (tested / 'process.o').read_bytes() == (production / 'process.o').read_bytes()
assert (tested / 'esp/boot/ai.pack').read_bytes() == (production / 'esp/boot/ai.pack').read_bytes()
compiler = (production / 'esp/boot/compiler.nos').read_bytes()
assert compiler == (tested / 'esp/boot/compiler.nos').read_bytes()
assert compiler == (ordinary / 'esp/boot/compiler.nos').read_bytes(), \
    'Ordinary/compiler guest payload differs'
assert b'build\\normal' not in compiler and b'build/normal' not in compiler
report.write_text(json.dumps({'passed': True, 'load_segments_match': list(apps),
    'compiler_byte_identical': True, 'scheduler_object_byte_identical': True,
    'model_pack_byte_identical': True}, indent=2) + '\n')
print('PASS: ten user payloads, scheduler, model pack and byte-identical compiler across variants')
