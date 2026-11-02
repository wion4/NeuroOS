"""Host tests execute the actual C memory-map validator via a freestanding DLL."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro


class Entry(c.Structure):
    _fields_ = [('base', c.c_uint64), ('length', c.c_uint64), ('type', c.c_uint64)]


class Map(c.Structure):
    _fields_ = [('revision', c.c_uint64), ('count', c.c_uint64),
                ('entries', c.POINTER(c.POINTER(Entry)))]


class Summary(c.Structure):
    _fields_ = [('bytes', c.c_uint64), ('regions', c.c_uint64)]


class MemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target = ROOT / 'build/host-tests'
        target.mkdir(parents=True, exist_ok=True)
        clang = neuro.tool('CLANG', 'clang', [r'C:\Program Files\LLVM\bin\clang.exe'])
        link = neuro.tool('LLD_LINK', 'lld-link', [r'C:\Program Files\LLVM\bin\lld-link.exe'])
        subprocess.run([clang, '--target=x86_64-pc-windows-msvc', '-ffreestanding',
                        '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
                        '-Ivendor/limine', '-c', 'kernel/memory.c', '-o',
                        str(target / 'memory.obj')], cwd=ROOT, check=True)
        subprocess.run([link, '/dll', '/noentry', '/nodefaultlib', '/export:memory_analyze',
                        str(target / 'memory.obj'), '/out:' + str(target / 'memory.dll')],
                       cwd=ROOT, check=True)
        cls.lib = c.CDLL(str(target / 'memory.dll'))
        cls.check = cls.lib.memory_analyze
        cls.check.argtypes = [c.POINTER(Map), c.POINTER(Summary)]
        cls.check.restype = c.c_bool

    def evaluate(self, ranges):
        entries = [Entry(*r) for r in ranges]
        pointers = (c.POINTER(Entry) * len(entries))(*(c.pointer(e) for e in entries))
        output = Summary(99, 99)
        passed = self.check(c.byref(Map(0, len(entries), pointers)), c.byref(output))
        return passed, output.bytes, output.regions

    def test_separated_usable_ranges_only(self):
        self.assertEqual(self.evaluate([(0, 4096, 1), (4096, 8192, 0),
                                       (12288, 4096, 5), (16384, 4096, 0)]),
                         (True, 12288, 2))

    def test_unsorted_adjacent_ranges(self):
        self.assertEqual(self.evaluate([(4096, 4096, 0), (0, 4096, 0)]), (True, 8192, 2))

    def test_overlapping_usable_ranges(self):
        self.assertEqual(self.evaluate([(0, 8192, 0), (4096, 8192, 0)]), (False, 0, 0))

    def test_usable_overlapping_reserved(self):
        self.assertEqual(self.evaluate([(0, 8192, 0), (4096, 4096, 1)]), (False, 0, 0))

    def test_overlapping_reserved_is_legal(self):
        self.assertEqual(self.evaluate([(0, 8192, 1), (4096, 8192, 1),
                                       (16384, 4096, 0)]), (True, 4096, 1))

    def test_range_end_overflow(self):
        self.assertEqual(self.evaluate([(2**64 - 4096, 4096, 0)]), (False, 0, 0))

    def test_zero_length(self):
        self.assertEqual(self.evaluate([(0, 0, 0)]), (False, 0, 0))

    def test_no_usable_ram(self):
        self.assertEqual(self.evaluate([(0, 4096, 1)]), (False, 0, 0))

    def test_empty_map(self):
        self.assertEqual(self.evaluate([]), (False, 0, 0))

    def test_null_inputs_and_entry(self):
        output = Summary(99, 99)
        self.assertFalse(self.check(None, c.byref(output)))
        self.assertEqual((output.bytes, output.regions), (0, 0))
        self.assertFalse(self.check(None, None))
        pointers = (c.POINTER(Entry) * 1)()
        self.assertFalse(self.check(c.byref(Map(0, 1, pointers)), c.byref(output)))

    def test_oversize_entry_count(self):
        entry = Entry(0, 4096, 0)
        pointers = (c.POINTER(Entry) * 1)(c.pointer(entry))
        self.assertFalse(self.check(c.byref(Map(0, 4097, pointers)), c.byref(Summary())))


if __name__ == '__main__':
    unittest.main(verbosity=2)
