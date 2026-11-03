"""Production UTF-8/editor/keymap code, tested under the host ABI."""
import ctypes as c
from pathlib import Path
import random
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import neuro
class Snapshot(c.Structure):
    _fields_=[('data',c.c_char*8193),('length',c.c_uint),('cursor',c.c_uint),('anchor',c.c_uint)]
class Document(c.Structure):
    _fields_=[('now',Snapshot),('history',Snapshot*9),('count',c.c_uint),('position',c.c_uint)]
class Input(c.Structure):
    _fields_=[('x',c.c_int),('y',c.c_int),('buttons',c.c_uint),('key',c.c_uint),('ticks',c.c_uint64),('mods',c.c_uint),('scan',c.c_uint)]
class Layout(c.Structure):_fields_=[('ru',c.c_bool),('chord',c.c_bool)]
class TextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        target=ROOT/'build/text-tests';target.mkdir(parents=True,exist_ok=True)
        clang=neuro.tool('CLANG','clang',[r'C:\Program Files\LLVM\bin\clang.exe'])
        linker=neuro.tool('LLD_LINK','lld-link',[r'C:\Program Files\LLVM\bin\lld-link.exe'])
        objects=[]
        for name in ('user/text.c','user/keyboard.c','kernel/runtime.c'):
            obj=target/(Path(name).stem+'.obj')
            subprocess.run([clang,'--target=x86_64-pc-windows-msvc','-O2','-ffreestanding','-fno-stack-protector','-fno-builtin','-Wall','-Wextra','-Werror','-Iuser','-c',name,'-o',str(obj)],cwd=ROOT,check=True)
            objects.append(str(obj))
        exports=['utf8_decode','utf8_encode','text_valid','text_load','text_insert','text_codepoint','text_erase','text_move','text_undo','text_position','text_hit','keyboard_translate']
        subprocess.run([linker,'/dll','/noentry','/nodefaultlib',*objects,*['/export:'+n for n in exports],'/out:'+str(target/'text.dll')],check=True)
        cls.lib=c.CDLL(str(target/'text.dll'))
        cls.lib.text_load.argtypes=[c.POINTER(Document),c.c_char_p,c.c_uint];cls.lib.text_load.restype=c.c_bool
        cls.lib.text_insert.argtypes=cls.lib.text_load.argtypes;cls.lib.text_insert.restype=c.c_bool
        cls.lib.text_valid.argtypes=[c.c_char_p,c.c_uint];cls.lib.text_valid.restype=c.c_bool
        cls.lib.text_codepoint.argtypes=[c.POINTER(Document),c.c_uint];cls.lib.text_codepoint.restype=c.c_bool
        cls.lib.text_erase.argtypes=[c.POINTER(Document),c.c_bool];cls.lib.text_erase.restype=c.c_bool
        cls.lib.text_undo.argtypes=[c.POINTER(Document),c.c_bool];cls.lib.text_undo.restype=c.c_bool
        cls.lib.text_move.argtypes=[c.POINTER(Document),c.c_uint,c.c_bool,c.c_bool,c.c_uint]
        cls.lib.keyboard_translate.argtypes=[c.POINTER(Layout),c.POINTER(Input)];cls.lib.keyboard_translate.restype=c.c_uint
    def doc(self,text=''):
        d=Document();data=text.encode();self.assertTrue(self.lib.text_load(c.byref(d),data,len(data)));return d
    def value(self,d):return bytes(d.now.data)[:d.now.length].decode()
    def test_valid_invalid_and_codepoint_roundtrip(self):
        rng=random.Random(8713)
        points=[0x20,0x7e,0xa0,0x401,0x451,0x800,0xd7ff,0xe000,0x10000,0x10ffff]
        points += [rng.randrange(0xa0,0x110000) for _ in range(1000)]
        for cp in points:
            if 0xd800<=cp<=0xdfff:continue
            raw=chr(cp).encode();out=c.create_string_buffer(4);decoded=c.c_uint()
            n=self.lib.utf8_encode(cp,out);self.assertEqual(out.raw[:n],raw)
            self.assertEqual(self.lib.utf8_decode(raw,len(raw),c.byref(decoded)),len(raw));self.assertEqual(decoded.value,cp)
            self.assertTrue(self.lib.text_valid(raw,len(raw)))
        for bad in (b'\x80',b'\xc0\x80',b'\xc1\xbf',b'\xe0\x80\xaf',b'\xed\xa0\x80',b'\xf4\x90\x80\x80',b'\xf5\x80\x80\x80',b'\xf0\x90',b'\0',b'\x1b',b'\x7f',b'\xc2\x80',b'\xd0x'):
            self.assertFalse(self.lib.text_valid(bad,len(bad)),bad)
    def test_multibyte_navigation_deletion_selection(self):
        d=self.doc('AПривет🙂Z')
        self.lib.text_move(c.byref(d),0x110003,False,False,46)
        self.assertTrue(self.lib.text_erase(c.byref(d),True));self.assertEqual(self.value(d),'AПриветZ')
        self.lib.text_move(c.byref(d),0x110005,False,True,46)
        self.lib.text_move(c.byref(d),0x110004,False,False,46)
        self.lib.text_move(c.byref(d),0x110004,True,False,46)
        self.assertTrue(self.lib.text_insert(c.byref(d),'Ё'.encode(),2));self.assertEqual(self.value(d),'AЁриветZ')
        self.assertTrue(self.lib.text_undo(c.byref(d),False));self.assertEqual(self.value(d),'AПриветZ')
        self.assertTrue(self.lib.text_undo(c.byref(d),True));self.assertEqual(self.value(d),'AЁриветZ')
    def test_capacity_failure_is_atomic_and_undo_branches(self):
        d=self.doc('x'*8191);before=bytes(d)
        self.assertFalse(self.lib.text_codepoint(c.byref(d),0x451));self.assertEqual(bytes(d),before)
        self.assertFalse(self.lib.text_insert(c.byref(d),b'\xff',1));self.assertEqual(bytes(d),before)
        self.assertTrue(self.lib.text_codepoint(c.byref(d),ord('a')))
        for _ in range(12):self.lib.text_erase(c.byref(d),True)
        for _ in range(8):self.assertTrue(self.lib.text_undo(c.byref(d),False))
        self.assertFalse(self.lib.text_undo(c.byref(d),False))
        self.lib.text_insert(c.byref(d),b'Z',1);self.assertFalse(self.lib.text_undo(c.byref(d),True))
    def test_wrapping_tabs_crlf_and_hit_testing(self):
        d=self.doc('абв\n\tX\r\nend');row=c.c_uint();col=c.c_uint()
        self.lib.text_position(c.byref(d),8,46,c.byref(row),c.byref(col))
        self.assertEqual((row.value,col.value),(1,4))
        self.assertEqual(self.lib.text_hit(c.byref(d),0,2,46),4)
        self.assertEqual(self.lib.text_hit(c.byref(d),1,2,46),7)
        self.lib.text_move(c.byref(d),0x110005,False,True,46)
        self.lib.text_move(c.byref(d),0x110006,False,False,46)
        self.assertEqual(d.now.cursor,6)
        self.lib.text_move(c.byref(d),0x110002,False,False,46)
        self.assertEqual(d.now.cursor,7)
    def test_layout_shift_caps_shortcuts_and_extended(self):
        layout=Layout();inp=Input(key=ord('q'),scan=16)
        self.assertEqual(self.lib.keyboard_translate(c.byref(layout),c.byref(inp)),ord('q'))
        inp.mods=5;inp.scan=0x2a;inp.key=0
        self.lib.keyboard_translate(c.byref(layout),c.byref(inp));self.assertTrue(layout.ru)
        self.lib.keyboard_translate(c.byref(layout),c.byref(inp));self.assertTrue(layout.ru)
        for mods,expected in ((0,'й'),(1,'Й'),(8,'Й'),(9,'й')):
            inp.mods=mods;inp.scan=16;inp.key=ord('q')
            self.assertEqual(self.lib.keyboard_translate(c.byref(layout),c.byref(inp)),ord(expected))
        inp.mods=2;inp.scan=31;inp.key=19
        self.assertEqual(self.lib.keyboard_translate(c.byref(layout),c.byref(inp)),19)
        inp.mods=0;inp.scan=0x153;inp.key=0
        self.assertEqual(self.lib.keyboard_translate(c.byref(layout),c.byref(inp)),0x110007)
        inp.scan=0x1d3;self.assertEqual(self.lib.keyboard_translate(c.byref(layout),c.byref(inp)),0)
if __name__=='__main__':unittest.main()
