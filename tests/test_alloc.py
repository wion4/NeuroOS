import ctypes as c
from pathlib import Path
import random
import subprocess
import unittest
from test_memory import Entry, Map, ROOT, neuro


class AllocTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target = ROOT / 'build/alloc-tests'
        target.mkdir(parents=True, exist_ok=True)
        clang = neuro.tool('CLANG', 'clang', [r'C:\Program Files\LLVM\bin\clang.exe'])
        linker = neuro.tool('LLD_LINK', 'lld-link', [r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects = []
        for source in ('memory', 'alloc', 'runtime'):
            obj = target / (source + '.obj')
            subprocess.run([clang, '--target=x86_64-pc-windows-msvc', '-ffreestanding',
                            '-fno-stack-protector', '-fno-builtin', '-Ivendor/limine',
                            '-Wall', '-Wextra', '-Werror', '-c', f'kernel/{source}.c',
                            '-o', str(obj)], cwd=ROOT, check=True)
            objects.append(str(obj))
        signatures = {
            'pmm_init': ([c.POINTER(Map)], c.c_bool),
            'pmm_alloc': ([c.c_uint64], c.c_uint64),
            'pmm_free': ([c.c_uint64, c.c_uint64], c.c_bool),
            'pmm_total': ([], c.c_uint64), 'pmm_available': ([], c.c_uint64),
            'heap_init': ([c.c_void_p, c.c_size_t], c.c_bool),
            'heap_alloc': ([c.c_size_t], c.c_void_p),
            'heap_free': ([c.c_void_p], c.c_bool), 'heap_available': ([], c.c_size_t)
        }
        subprocess.run([linker, '/dll', '/noentry', '/nodefaultlib', *objects,
                        *('/export:' + n for n in signatures),
                        '/out:' + str(target / 'alloc.dll')], cwd=ROOT, check=True)
        cls.lib = c.CDLL(str(target / 'alloc.dll'))
        for name, (args, result) in signatures.items():
            fn = getattr(cls.lib, name); fn.argtypes = args; fn.restype = result

    def init_map(self, entries):
        records = [Entry(*e) for e in entries]
        pointers = (c.POINTER(Entry) * len(records))(*(c.pointer(e) for e in records))
        return self.lib.pmm_init(c.byref(Map(0, len(records), pointers)))

    def setUp(self):
        self.assertTrue(self.init_map([(4096, 4096 * 32, 0)]))
        self.arena = c.create_string_buffer(16384 + 16)
        self.address = (c.addressof(self.arena) + 15) & ~15
        self.assertTrue(self.lib.heap_init(self.address, 16384))

    def test_pages_exhaust_reuse_double_free(self):
        addresses = [self.lib.pmm_alloc(1) for _ in range(32)]
        self.assertEqual(len(set(addresses)), 32)
        self.assertTrue(all(a and a % 4096 == 0 for a in addresses))
        self.assertEqual(self.lib.pmm_alloc(1), 0)
        self.assertTrue(self.lib.pmm_free(addresses[7], 1))
        self.assertFalse(self.lib.pmm_free(addresses[7], 1))
        self.assertEqual(self.lib.pmm_alloc(1), addresses[7])

    def test_pages_alignment_reserved_holes(self):
        self.assertTrue(self.init_map([(1, 12287, 0), (12288, 4096, 1), (16384, 8192, 0)]))
        self.assertEqual(self.lib.pmm_total(), 4)
        self.assertEqual(self.lib.pmm_alloc(3), 0)
        self.assertEqual(self.lib.pmm_alloc(2), 4096)
        self.assertEqual(self.lib.pmm_alloc(2), 16384)

    def test_pages_invalid_free_is_atomic(self):
        a = self.lib.pmm_alloc(2)
        before = self.lib.pmm_available()
        for addr, size in [(a+1, 1), (0, 1), (a, 0), (a, 3), (1 << 63, 1), (a, 2**64-1)]:
            self.assertFalse(self.lib.pmm_free(addr, size))
            self.assertEqual(self.lib.pmm_available(), before)
        self.assertTrue(self.lib.pmm_free(a, 2))

    def test_pages_zero_and_oversize(self):
        self.assertEqual(self.lib.pmm_alloc(0), 0)
        self.assertEqual(self.lib.pmm_alloc(2**64-1), 0)
        self.assertFalse(self.init_map([(4096, (64*1024**3)+4096, 0)]))
        self.assertEqual(self.lib.pmm_available(), 0)

    def test_heap_alignment_coalesce_and_invalid_free(self):
        before = self.lib.heap_available()
        a, b, d = [self.lib.heap_alloc(n) for n in (1, 33, 77)]
        self.assertTrue(all(p % 16 == 0 for p in (a,b,d)))
        self.assertFalse(self.lib.heap_free(a + 1))
        self.assertFalse(self.lib.heap_free(1))
        self.assertTrue(self.lib.heap_free(b))
        self.assertTrue(self.lib.heap_free(a))
        self.assertTrue(self.lib.heap_free(d))
        self.assertFalse(self.lib.heap_free(d))
        self.assertEqual(self.lib.heap_available(), before)
        self.assertIsNotNone(self.lib.heap_alloc(before))
        self.assertIsNone(self.lib.heap_alloc(1))

    def test_heap_zero_overflow_invalid_init(self):
        self.assertIsNone(self.lib.heap_alloc(0))
        self.assertIsNone(self.lib.heap_alloc(2**64-1))
        self.assertFalse(self.lib.heap_init(self.address+1, 100))
        self.assertFalse(self.lib.heap_init(self.address, 20))

    def test_randomized_heap_patterns(self):
        rng = random.Random(713)
        live = []
        before = self.lib.heap_available()
        for step in range(500):
            for addr, size, value in live:
                self.assertEqual(c.string_at(addr, size), bytes([value])*size)
            if live and rng.randrange(3) == 0:
                addr, _, _ = live.pop(rng.randrange(len(live)))
                self.assertTrue(self.lib.heap_free(addr))
            else:
                size = rng.randrange(1, 1000)
                addr = self.lib.heap_alloc(size)
                if addr:
                    value = step % 256
                    c.memset(addr, value, size)
                    live.append((addr,size,value))
        for addr,_,_ in live: self.assertTrue(self.lib.heap_free(addr))
        self.assertEqual(self.lib.heap_available(), before)

    def test_randomized_page_runs(self):
        rng = random.Random(17)
        live = []
        occupied = set()
        for _ in range(500):
            if live and rng.randrange(2):
                addr, pages = live.pop(rng.randrange(len(live)))
                self.assertTrue(self.lib.pmm_free(addr, pages))
                occupied.difference_update(range(addr//4096, addr//4096+pages))
            else:
                pages = rng.randrange(1, 9); addr = self.lib.pmm_alloc(pages)
                if addr:
                    new = set(range(addr//4096, addr//4096+pages))
                    self.assertFalse(occupied & new)
                    occupied.update(new); live.append((addr,pages))
            self.assertEqual(self.lib.pmm_available(), 32-len(occupied))


if __name__ == '__main__': unittest.main(verbosity=2)
