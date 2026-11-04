"""Real journal C, independent observers, bounded retention and uint64 boundary."""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import neuro

class Event(c.Structure):
    _fields_ = [('sequence',c.c_uint64),('ticks',c.c_uint64),('job',c.c_uint64),
        ('owner',c.c_uint64),('kind',c.c_uint32),('flags',c.c_uint32),
        ('value',c.c_uint64),('cpu_ticks',c.c_uint64),('pages',c.c_uint64)]
class Query(c.Structure):
    _fields_ = [('version',c.c_uint32),('capacity',c.c_uint32),('after',c.c_uint64),
        ('next',c.c_uint64),('oldest',c.c_uint64),('lost',c.c_uint64),
        ('count',c.c_uint32),('flags',c.c_uint32),('events',Event*8)]

class JournalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target = ROOT/'build/journal-tests'; target.mkdir(parents=True,exist_ok=True)
        clang = neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker = neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for source in ('tests/journal_harness.c','kernel/runtime.c'):
            obj=target/(Path(source).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding',
                '-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror',
                '-c',str(ROOT/source),'-o',str(obj)],check=True)
            objects.append(str(obj))
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,
            '/export:journal_reset','/export:journal_append','/export:journal_read',
            '/export:journal_test_seed','/out:'+str(target/'journal.dll')],check=True)
        cls.lib=c.CDLL(str(target/'journal.dll'))
        cls.lib.journal_append.argtypes=[c.POINTER(Event)]
        cls.lib.journal_read.argtypes=[c.POINTER(Query)]
        cls.lib.journal_read.restype=c.c_int
        cls.lib.journal_test_seed.argtypes=[c.c_uint64]
    def setUp(self):self.lib.journal_reset()
    def append(self,number):
        event=Event(sequence=999,ticks=number*10,job=number,owner=0x100000001,
                    kind=5,flags=1,value=42,cpu_ticks=number,pages=100+number)
        self.lib.journal_append(c.byref(event))
        self.assertEqual(event.sequence,999) # Input is not modified.
    def read(self,after=0,capacity=8):
        query=Query(version=1,capacity=capacity,after=after)
        self.assertEqual(self.lib.journal_read(c.byref(query)),0)
        return query
    def test_wire_sizes_and_empty(self):
        self.assertEqual(c.sizeof(Event),64);self.assertEqual(c.sizeof(Query),560)
        query=self.read()
        self.assertEqual((query.next,query.oldest,query.lost,query.count,query.flags),(0,1,0,0,0))
        self.assertEqual(bytes(query.events),bytes(512))
    def test_numeric_metadata_and_sequence(self):
        for i in range(1,4):self.append(i)
        query=self.read()
        self.assertEqual((query.count,query.next,query.lost),(3,3,0))
        for i,event in enumerate(query.events[:3],1):
            self.assertEqual((event.sequence,event.ticks,event.job,event.owner,event.kind,
                event.flags,event.value,event.cpu_ticks,event.pages),
                (i,i*10,i,0x100000001,5,1,42,i,100+i))
    def test_readers_do_not_consume_each_other(self):
        for i in range(1,4):self.append(i)
        first=self.read();second=self.read()
        self.assertEqual(bytes(first),bytes(second))
        self.append(4)
        self.assertEqual(self.read(after=first.next).events[0].job,4)
        self.assertEqual(self.read().count,4)
    def test_overflow_loss_and_paging(self):
        for i in range(1,101):self.append(i)
        query=self.read()
        self.assertEqual((query.oldest,query.lost,query.count,query.next),(37,36,8,44))
        seen=[]
        while query.count:
            seen.extend(e.sequence for e in query.events[:query.count])
            query=self.read(after=query.next)
        self.assertEqual(seen,list(range(37,101)))
        self.assertEqual((query.next,query.lost),(100,0))
    def test_invalid_input_is_atomic(self):
        self.append(1)
        for version,capacity,after in ((0,8,0),(2,8,0),(1,0,0),(1,9,0),(1,8,2)):
            query=Query.from_buffer_copy(b'Z'*560)
            query.version,query.capacity,query.after=version,capacity,after
            before=bytes(query)
            self.assertEqual(self.lib.journal_read(c.byref(query)),-22)
            self.assertEqual(bytes(query),before)
        self.assertEqual(self.read().next,1)
    def test_short_batches_zero_unused_records(self):
        for i in range(1,4):self.append(i)
        query=self.read(after=1,capacity=1)
        self.assertEqual((query.next,query.count,query.events[0].sequence),(2,1,2))
        self.assertEqual(bytes(query.events)[64:],bytes(448))
    def test_maximum_sequence_does_not_wrap(self):
        maximum=2**64-1
        self.lib.journal_test_seed(maximum-64)
        for i in range(1,65):self.append(i)
        self.append(65) # Saturates instead of overwriting with sequence zero.
        query=self.read(after=maximum-1)
        self.assertEqual((query.count,query.next,query.flags),(1,maximum,1))
        self.assertEqual((query.events[0].sequence,query.events[0].job),(maximum,64))
        query=self.read(after=maximum)
        self.assertEqual((query.count,query.next,query.flags),(0,maximum,1))
        query=self.read(capacity=1)
        self.assertEqual((query.oldest,query.lost),(maximum-63,maximum-64))
        self.lib.journal_reset()
        self.assertEqual((self.read().next,self.read().flags),(0,0))

if __name__=='__main__':unittest.main(verbosity=2)
